#include "vr.h"

#ifdef VR

#include <thread>
#include <chrono>
#include <fstream>

#include "painter.h"
#include "particlepainter.h"
#include "meshpainter.h"
//========= Copyright Valve Corporation ============//

void ThreadSleep(unsigned long nMilliseconds)
{
#if defined(_WIN32)
::Sleep(nMilliseconds);
#elif defined(POSIX)
usleep(nMilliseconds * 1000);
#endif
}

std::chrono::milliseconds start_time = std::chrono::milliseconds(0);

namespace Manta {

std::vector<Painter*> mPainter;

	// Macro to iterate through one plane
#define FOR_P_SLICE(__g,__dim,__plane) \
for(Vec3i __g0(__fRange(Vec3i(0,0,0),__dim,__plane)), __g1(__fRange((__g)->getSize(),__dim,__plane+1)), p(__g0); p.z<__g1.z; p.z++) \
	for(p.y=__g0.y; p.y < __g1.y; p.y++) \
		for(p.x=__g0.x; p.x < __g1.x; p.x++)
inline Vec3i __fRange(Vec3i size, int dim, int plane) { Vec3i p(size); p[dim] = plane; return p; }

PbClass* mObject = nullptr;
int mObjIndex = -1;
Real        mMaxVal;       //! stats
int         mDim = 2, mPlane, mMax;
Grid<Vec3>* mLocalGrid;    //! currently selected grid
FlagGrid** mFlags;        //! flag grid (can influence display of selected grid)
QLabel* mInfo;         //! info string
bool        mHide;         //! hide all grids?
bool        mHideLocal;    //! hide only this type?
std::map< void*, int > mDispMode; //! display modes , for each object
std::map< std::pair<void*, int>, Real> mValScale; //! scaling of values , per object and display mode


void getCellCoordinatesVR(const Vec3i& pos, Vec3 box[4], int dim, bool offset = false) {
	int dim2 = (dim + 1) % 3;
	Vec3 p0(pos.x, pos.y, pos.z);
	Vec3 p1(pos.x + 1, pos.y + 1, pos.z + 1);
	p1[dim] = p0[dim] = pos[dim] + 0.5;

	// display lines with slight offsets
	if (offset) {
		p0 += Vec3(0.01);
		p1 -= Vec3(0.01);
	}

	box[0] = p0;
	box[3] = p0; box[3][dim2] = p1[dim2];
	box[1] = p1; box[1][dim2] = p0[dim2];
	box[2] = p1;
}

std::ofstream fout;

static inline void glVertexVR(const Vec3& v, const float dx, std::vector<float>* lines) {
	float scale = 5.0;

	float x = v.x * dx * scale;
	x -= 2;

	float y = v.y * dx * scale;

	float z = v.z * dx * scale;
	z -= 2;

	lines->push_back(z);
	lines->push_back(y);
	lines->push_back(x);

	
	
}

void glBoxVR(const Vec3& p0, const Vec3& p1, const float dx) {
	// XXX/bmoody Review this
	std::vector<float> lines;
	const int box[24] = { 0,1,0,2,0,4,7,6,7,5,7,3,1,3,1,5,2,3,2,6,4,5,4,6 };
	for (int i = 0; i < 24; i++) {
		const int b = box[i];
		glVertexVR(Vec3((b & 1) ? p1.x : p0.x, (b & 2) ? p1.y : p0.y, (b & 4) ? p1.z : p0.z), dx, &lines);
	}
}
template<typename T>
void update() {
	Grid<T>* src = (Grid<T>*) mObject;

	if (!mLocalGrid) {
		mLocalGrid = new Grid<T>(src->getParent());
	}

	mLocalGrid->copyFrom(*src, true); // copy grid data and type marker
	mLocalGrid->setName(src->getName());
	mLocalGrid->setParent(src->getParent());
	mMaxVal = mLocalGrid->getMaxAbs();

	mPlane = clamp(mPlane, 0, mLocalGrid->getSize()[mDim] - 1);
}

void nextObject() {
	if (PbClass::getNumInstances() == 0) return;

	int oldIndex = mObjIndex;
	for (;;) {
		mObjIndex = (mObjIndex + 1) % PbClass::getNumInstances();
		if (oldIndex == mObjIndex) break;

		PbClass* obj = PbClass::getInstance(mObjIndex);
		if (obj->canConvertTo("Grid<Vec3>") && !obj->isHidden()) {
			mObject = obj;
			update<Vec3>();
			return;
		}
		if (oldIndex < 0) oldIndex = 0; // prevent endless loop on first run
	}
}

int getDispMode() {
	if (!mObject || !mLocalGrid) return Painter::RealDisplayModes::RealDispOff;
	if (mDispMode.find(mObject) == mDispMode.end()) {
		int dm = Painter::RealDisplayModes::RealDispStd; // same for vec & real
		// initialize exceptions, eg levelset
		if (mLocalGrid->getType() & GridBase::TypeLevelset) dm = Painter::RealDisplayModes::RealDispLevelset;
		mDispMode[mObject] = dm;
		return dm;
	}
	return mDispMode[mObject];
}

Real getScale() {
	if (!mObject) return 0;
	const int dm = getDispMode();
	std::pair<void*, int> id; id.first = mObject; id.second = dm;

	if (mValScale.find(id) == mValScale.end()) {
		// init new scale value
		Real s = 1.0 - VECTOR_EPSILON;
		if (mLocalGrid->getType() & GridBase::TypeVec3)
			s = 0.5 - VECTOR_EPSILON;
		else if (mLocalGrid->getType() & GridBase::TypeLevelset)
			s = 1.0;
		else if (mLocalGrid->getType() & GridBase::TypeReal) {
			if (dm == Painter::RealDisplayModes::RealDispShadeVol) s = 4.0; // depends a bit on grid size in practice...
			if (dm == Painter::RealDisplayModes::RealDispShadeSurf) s = 1.0;
		}
		mValScale[id] = s;
	}
	return mValScale[id];

}

std::vector<float> get_lines() {
	std::vector<float> lines;

	if (!mObject || mPlane < 0 || mPlane >= mLocalGrid->getSize()[mDim])
		return lines;

	const int dm = getDispMode();
	const Real scale = getScale();
	const float dx = mLocalGrid->getDx();
	const bool mac = mLocalGrid->getType() & GridBase::TypeMAC;

	if ((dm == Painter::VecDisplayModes::VecDispCentered) || (dm == Painter::VecDisplayModes::VecDispStaggered)) {
		FOR_P_SLICE(mLocalGrid, mDim, mPlane) {
			Vec3 vel = mLocalGrid->get(p) * scale;
			Vec3 pos(p.x + 0.5, p.y + 0.5, p.z + 0.5);
			if (dm == Painter::VecDisplayModes::VecDispCentered) {
				if (mac) {
					if (p.x < mLocalGrid->getSizeX() - 1)
						vel.x = 0.5 * (vel.x + scale * mLocalGrid->get(p.x + 1, p.y, p.z).x);
					if (p.y < mLocalGrid->getSizeY() - 1)
						vel.y = 0.5 * (vel.y + scale * mLocalGrid->get(p.x, p.y + 1, p.z).y);
					if (p.z < mLocalGrid->getSizeZ() - 1)
						vel.z = 0.5 * (vel.z + scale * mLocalGrid->get(p.x, p.y, p.z + 1).z);
				}
				glVertexVR(pos, dx, &lines);
				// glVertexVR(pos + vel * 1.2, dx, &lines);
			}
			else if (dm == Painter::VecDisplayModes::VecDispStaggered) {
				for (int d = 0; d < 3; d++) {
					if (fabs(vel[d]) < 1e-2) continue;
					Vec3 p1(pos);
					if (mac)
						p1[d] -= 0.5f;
					Vec3 color(0.0);
					color[d] = 1;
					glVertexVR(p1, dx, &lines);
					p1[d] += vel[d];
					glVertexVR(p1, dx, &lines);
				}
			}
		}
	}
	else if (dm == Painter::VecDisplayModes::VecDispUv) {
		std::cout << "Painting UV" << std::endl;
		// draw as "uv" coordinates (ie rgb), note - this will completely hide the real grid display!
		Vec3 box[4];
		// XXX/bmoody Don't want lines here, review
		FOR_P_SLICE(mLocalGrid, mDim, mPlane)
		{
			Vec3 v = mLocalGrid->get(p) * scale;
			if (mac) {
				if (p.x < mLocalGrid->getSizeX() - 1) v.x = 0.5 * (v.x + scale * mLocalGrid->get(p.x + 1, p.y, p.z).x);
				if (p.y < mLocalGrid->getSizeY() - 1) v.y = 0.5 * (v.y + scale * mLocalGrid->get(p.x, p.y + 1, p.z).y);
				if (p.z < mLocalGrid->getSizeZ() - 1) v.z = 0.5 * (v.z + scale * mLocalGrid->get(p.x, p.y, p.z + 1).z);
			}
			for (int c = 0; c < 3; ++c) {
				if (v[c] < 0.) v[c] *= -1.;
				v[c] = fmod((Real)v[c], (Real)1.);
			}
			//v *= mLocalGrid->get(0)[0]; // debug, show uv grid weight as brightness of values
			getCellCoordinatesVR(p, box, mDim);
			for (int n = 0; n < 4; n++)
				glVertexVR(box[n], dx, &lines);
		}
	}

	return lines;
}

void GLAPIENTRY
ErrorCallback(GLenum source,
	GLenum type,
	GLuint id,
	GLenum severity,
	GLsizei length,
	const GLchar* message,
	const void* userParam)
{

	std::cout << message << std::endl;
}

//---------------------------------------------------------------------------------------------------------------------
// Purpose: Returns true if the action is active and had a rising edge
//---------------------------------------------------------------------------------------------------------------------
bool GetDigitalActionRisingEdge(vr::VRActionHandle_t action, vr::VRInputValueHandle_t* pDevicePath)
{
	vr::InputDigitalActionData_t actionData;
	vr::VRInput()->GetDigitalActionData(action, &actionData, sizeof(actionData), vr::k_ulInvalidInputValueHandle);
	if (pDevicePath)
	{
		*pDevicePath = vr::k_ulInvalidInputValueHandle;
		if (actionData.bActive)
		{
			vr::InputOriginInfo_t originInfo;
			if (vr::VRInputError_None == vr::VRInput()->GetOriginTrackedDeviceInfo(actionData.activeOrigin, &originInfo, sizeof(originInfo)))
			{
				*pDevicePath = originInfo.devicePath;
			}
		}
	}
	return actionData.bActive && actionData.bChanged && actionData.bState;
}


//---------------------------------------------------------------------------------------------------------------------
// Purpose: Returns true if the action is active and had a falling edge
//---------------------------------------------------------------------------------------------------------------------
bool GetDigitalActionFallingEdge(vr::VRActionHandle_t action, vr::VRInputValueHandle_t* pDevicePath)
{
	vr::InputDigitalActionData_t actionData;
	vr::VRInput()->GetDigitalActionData(action, &actionData, sizeof(actionData), vr::k_ulInvalidInputValueHandle);
	if (pDevicePath)
	{
		*pDevicePath = vr::k_ulInvalidInputValueHandle;
		if (actionData.bActive)
		{
			vr::InputOriginInfo_t originInfo;
			if (vr::VRInputError_None == vr::VRInput()->GetOriginTrackedDeviceInfo(actionData.activeOrigin, &originInfo, sizeof(originInfo)))
			{
				*pDevicePath = originInfo.devicePath;
			}
		}
	}
	return actionData.bActive && actionData.bChanged && !actionData.bState;
}


//---------------------------------------------------------------------------------------------------------------------
// Purpose: Returns true if the action is active and its state is true
//---------------------------------------------------------------------------------------------------------------------
bool GetDigitalActionState(vr::VRActionHandle_t action, vr::VRInputValueHandle_t* pDevicePath)
{
	vr::InputDigitalActionData_t actionData;
	vr::VRInput()->GetDigitalActionData(action, &actionData, sizeof(actionData), vr::k_ulInvalidInputValueHandle);
	if (pDevicePath)
	{
		*pDevicePath = vr::k_ulInvalidInputValueHandle;
		if (actionData.bActive)
		{
			vr::InputOriginInfo_t originInfo;
			if (vr::VRInputError_None == vr::VRInput()->GetOriginTrackedDeviceInfo(actionData.activeOrigin, &originInfo, sizeof(originInfo)))
			{
				*pDevicePath = originInfo.devicePath;
			}
		}
	}
	return actionData.bActive && actionData.bState;
}

//-----------------------------------------------------------------------------
// Purpose: Outputs a set of optional arguments to debugging output, using
//          the printf format setting specified in fmt*.
//-----------------------------------------------------------------------------
void dprintf(const char* fmt, ...)
{
	va_list args;
	char buffer[2048];

	va_start(args, fmt);
	vsprintf_s(buffer, fmt, args);
	va_end(args);

	if (g_bPrintf)
		printf("%s", buffer);

	OutputDebugStringA(buffer);
}

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CMainApplication::CMainApplication(int argc, char* argv[])
	: m_pCompanionWindow(NULL)
	, m_pContext(NULL)
	, m_nCompanionWindowWidth(640)
	, m_nCompanionWindowHeight(320)
	, m_unSceneProgramID(0)
	, m_unCompanionWindowProgramID(0)
	, m_unControllerTransformProgramID(0)
	, m_unRenderModelProgramID(0)
	, m_unTestProgramID(0)
	, m_pHMD(NULL)
	, m_bDebugOpenGL(false)
	, m_bVerbose(false)
	, m_bPerf(false)
	, m_bVblank(false)
	, m_bGlFinishHack(true)
	, m_glControllerVertBuffer(0)
	, m_unControllerVAO(0)
	, m_unSceneVAO(0)
	, m_unTestVAO(0)
	, m_nSceneMatrixLocation(-1)
	, m_nControllerMatrixLocation(-1)
	, m_nRenderModelMatrixLocation(-1)
	, m_nTestMatrixLocation(-1)
	, m_iTrackedControllerCount(0)
	, m_iTrackedControllerCount_Last(-1)
	, m_iValidPoseCount(0)
	, m_iValidPoseCount_Last(-1)
	, m_iSceneVolumeInit(2)
	, m_strPoseClasses("")
	, m_bShowCubes(true)
{

	for (int i = 1; i < argc; i++)
	{
		if (!stricmp(argv[i], "-gldebug"))
		{
			m_bDebugOpenGL = true;
		}
		else if (!stricmp(argv[i], "-verbose"))
		{
			m_bVerbose = true;
		}
		else if (!stricmp(argv[i], "-novblank"))
		{
			m_bVblank = false;
		}
		else if (!stricmp(argv[i], "-noglfinishhack"))
		{
			m_bGlFinishHack = false;
		}
		else if (!stricmp(argv[i], "-noprintf"))
		{
			g_bPrintf = false;
		}
		else if (!stricmp(argv[i], "-cubevolume") && (argc > i + 1) && (*argv[i + 1] != '-'))
		{
			m_iSceneVolumeInit = atoi(argv[i + 1]);
			i++;
		}
	}
	// other initialization tasks are done in BInit
	memset(m_rDevClassChar, 0, sizeof(m_rDevClassChar));
};


//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
CMainApplication::~CMainApplication()
{
	// work is done in Shutdown
	dprintf("Shutdown");
}

GLuint CMainApplication::getBufferId()
{
	GLuint buffer;
	glGenBuffers(1, &buffer);
	return buffer;
}


#define BUFFER_OFFSET(bytes) ((GLubyte*) NULL + (bytes)) 
void CMainApplication::drawLines(GLuint buffer, std::vector<float>& vertices, std::vector<float>& colors)
{
	glBindBuffer(GL_ARRAY_BUFFER, buffer);

	int vertexbufsize = vertices.size() * sizeof(float);
	int colorbufsize = colors.size() * sizeof(float);
	glBufferData(GL_ARRAY_BUFFER, vertexbufsize + colorbufsize, NULL, GL_STATIC_DRAW); // data=NULL : initialize, but don't copy

	glBufferSubData(GL_ARRAY_BUFFER, 0, vertexbufsize, &vertices[0]);
	glBufferSubData(GL_ARRAY_BUFFER, vertexbufsize, colorbufsize, &colors[0]);

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	glVertexPointer(3, GL_FLOAT, 0, BUFFER_OFFSET(0));
	glColorPointer(3, GL_FLOAT, 0, BUFFER_OFFSET(vertexbufsize));

	// glDrawElements(GL_LINES, vertices.size(), GL_UNSIGNED_SHORT, BUFFER_OFFSET(0));
	glDrawArrays(GL_LINES, 0, vertices.size() / 3);
}

void CMainApplication::drawTriangles(GLuint buffer, std::vector<float>& vertices, std::vector<float>& colors)
{
	glBindBuffer(GL_ARRAY_BUFFER, buffer);

	int vertexbufsize = vertices.size() * sizeof(float);
	int colorbufsize = colors.size() * sizeof(float);
	glBufferData(GL_ARRAY_BUFFER, vertexbufsize + colorbufsize, NULL, GL_STATIC_DRAW); // data=NULL : initialize, but don't copy

	glBufferSubData(GL_ARRAY_BUFFER, 0, vertexbufsize, &vertices[0]);
	glBufferSubData(GL_ARRAY_BUFFER, vertexbufsize, colorbufsize, &colors[0]);

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	glVertexPointer(3, GL_FLOAT, 0, BUFFER_OFFSET(0));
	glColorPointer(3, GL_FLOAT, 0, BUFFER_OFFSET(vertexbufsize));

	// glDrawElements(GL_LINES, vertices.size(), GL_UNSIGNED_SHORT, BUFFER_OFFSET(0));
	glDrawArrays(GL_TRIANGLES, 0, vertices.size() / 3);
}



//-----------------------------------------------------------------------------
// Purpose: Helper to get a string from a tracked device property and turn it
//			into a std::string
//-----------------------------------------------------------------------------
std::string GetTrackedDeviceString(vr::TrackedDeviceIndex_t unDevice, vr::TrackedDeviceProperty prop, vr::TrackedPropertyError* peError)
{
	uint32_t unRequiredBufferLen = vr::VRSystem()->GetStringTrackedDeviceProperty(unDevice, prop, NULL, 0, peError);
	if (unRequiredBufferLen == 0)
		return "";

	char* pchBuffer = new char[unRequiredBufferLen];
	unRequiredBufferLen = vr::VRSystem()->GetStringTrackedDeviceProperty(unDevice, prop, pchBuffer, unRequiredBufferLen, peError);
	std::string sResult = pchBuffer;
	delete[] pchBuffer;
	return sResult;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainApplication::BInit()
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0)
	{
		printf("%s - SDL could not initialize! SDL Error: %s\n", __FUNCTION__, SDL_GetError());
		return false;
	}

	// Loading the SteamVR Runtime
	vr::EVRInitError eError = vr::VRInitError_None;
	m_pHMD = vr::VR_Init(&eError, vr::VRApplication_Scene);

	if (eError != vr::VRInitError_None)
	{
		m_pHMD = NULL;
		char buf[1024];
		sprintf_s(buf, sizeof(buf), "Unable to init VR runtime: %s", vr::VR_GetVRInitErrorAsEnglishDescription(eError));
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "VR_Init Failed", buf, NULL);
		return false;
	}


	int nWindowPosX = 700;
	int nWindowPosY = 100;
	Uint32 unWindowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	//SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY );
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
	if (m_bDebugOpenGL)
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);

	m_pCompanionWindow = SDL_CreateWindow("hellovr", nWindowPosX, nWindowPosY, m_nCompanionWindowWidth, m_nCompanionWindowHeight, unWindowFlags);
	if (m_pCompanionWindow == NULL)
	{
		printf("%s - Window could not be created! SDL Error: %s\n", __FUNCTION__, SDL_GetError());
		return false;
	}

	m_pContext = SDL_GL_CreateContext(m_pCompanionWindow);
	if (m_pContext == NULL)
	{
		printf("%s - OpenGL context could not be created! SDL Error: %s\n", __FUNCTION__, SDL_GetError());
		return false;
	}

	glewExperimental = GL_TRUE;
	GLenum nGlewError = glewInit();
	if (nGlewError != GLEW_OK)
	{
		printf("%s - Error initializing GLEW! %s\n", __FUNCTION__, glewGetErrorString(nGlewError));
		return false;
	}
	glGetError(); // to clear the error caused deep in GLEW

	if (SDL_GL_SetSwapInterval(m_bVblank ? 1 : 0) < 0)
	{
		printf("%s - Warning: Unable to set VSync! SDL Error: %s\n", __FUNCTION__, SDL_GetError());
		return false;
	}

	m_strDriver = "No Driver";
	m_strDisplay = "No Display";

	m_strDriver = GetTrackedDeviceString(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_TrackingSystemName_String);
	m_strDisplay = GetTrackedDeviceString(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_SerialNumber_String);

	std::string strWindowTitle = "hellovr - " + m_strDriver + " " + m_strDisplay;
	SDL_SetWindowTitle(m_pCompanionWindow, strWindowTitle.c_str());

	// cube array
	m_iSceneVolumeWidth = m_iSceneVolumeInit;
	m_iSceneVolumeHeight = m_iSceneVolumeInit;
	m_iSceneVolumeDepth = m_iSceneVolumeInit;

	m_fScale = 0.3f;
	m_fScaleSpacing = 4.0f;

	m_fNearClip = 0.1f;
	m_fFarClip = 30.0f;

	m_iTexture = 0;
	m_uiVertcount = 0;

	// 		m_MillisecondsTimer.start(1, this);
	// 		m_SecondsTimer.start(1000, this);

	printf("Waiting for simulation");
	std::this_thread::sleep_for(std::chrono::seconds(2));
	start_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());

	if (!BInitGL())
	{
		printf("%s - Unable to initialize OpenGL!\n", __FUNCTION__);
		return false;
	}

	if (!BInitCompositor())
	{
		printf("%s - Failed to initialize VR Compositor!\n", __FUNCTION__);
		return false;
	}

	vr::VRInput()->SetActionManifestPath(Path_MakeAbsolute("../hellovr_actions.json", Path_StripFilename(Path_GetExecutablePath())).c_str());

	vr::VRInput()->GetActionHandle("/actions/demo/in/HideCubes", &m_actionHideCubes);
	vr::VRInput()->GetActionHandle("/actions/demo/in/HideThisController", &m_actionHideThisController);
	vr::VRInput()->GetActionHandle("/actions/demo/in/TriggerHaptic", &m_actionTriggerHaptic);
	vr::VRInput()->GetActionHandle("/actions/demo/in/AnalogInput", &m_actionAnalongInput);

	vr::VRInput()->GetActionSetHandle("/actions/demo", &m_actionsetDemo);

	vr::VRInput()->GetActionHandle("/actions/demo/out/Haptic_Left", &m_rHand[Left].m_actionHaptic);
	vr::VRInput()->GetInputSourceHandle("/user/hand/left", &m_rHand[Left].m_source);
	vr::VRInput()->GetActionHandle("/actions/demo/in/Hand_Left", &m_rHand[Left].m_actionPose);

	vr::VRInput()->GetActionHandle("/actions/demo/out/Haptic_Right", &m_rHand[Right].m_actionHaptic);
	vr::VRInput()->GetInputSourceHandle("/user/hand/right", &m_rHand[Right].m_source);
	vr::VRInput()->GetActionHandle("/actions/demo/in/Hand_Right", &m_rHand[Right].m_actionPose);

	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(ErrorCallback, 0);

//	GridPainter<int>* intPainter = new GridPainter<int>(NULL);
//	mPainter.push_back(new GridPainter<Real>((FlagGrid**)intPainter->getGridPtr()));
	mPainter.push_back(new GridPainter<Vec3>(NULL));
//	mPainter.push_back(intPainter);
//	mPainter.push_back(new ParticlePainter(intPainter));
//	MeshPainter* ptr = new MeshPainter();
//	mPainter.push_back(ptr);

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Outputs the string in message to debugging output.
//          All other parameters are ignored.
//          Does not return any meaningful value or reference.
//-----------------------------------------------------------------------------
void APIENTRY DebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const char* message, const void* userParam)
{
	dprintf("GL Error: %s\n", message);
}



//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainApplication::SetupTexturemaps()
{
	std::string sExecutableDirectory = Path_StripFilename(Path_GetExecutablePath());
	std::string strFullPath = Path_MakeAbsolute("../cube_texture.png", sExecutableDirectory);

	std::vector<unsigned char> imageRGBA;
	unsigned nImageWidth, nImageHeight;
	unsigned nError = lodepng::decode(imageRGBA, nImageWidth, nImageHeight, strFullPath.c_str());

	if (nError != 0)
		return false;

	glGenTextures(1, &m_iTexture);
	glBindTexture(GL_TEXTURE_2D, m_iTexture);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, nImageWidth, nImageHeight,
		0, GL_RGBA, GL_UNSIGNED_BYTE, &imageRGBA[0]);

	glGenerateMipmap(GL_TEXTURE_2D);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

	GLfloat fLargest;
	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &fLargest);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, fLargest);

	glBindTexture(GL_TEXTURE_2D, 0);

	return (m_iTexture != 0);
}


//-----------------------------------------------------------------------------
// Purpose: Initialize OpenGL. Returns true if OpenGL has been successfully
//          initialized, false if shaders could not be created.
//          If failure occurred in a module other than shaders, the function
//          may return true or throw an error. 
//-----------------------------------------------------------------------------
bool CMainApplication::BInitGL()
{
	if (m_bDebugOpenGL)
	{
		glDebugMessageCallback((GLDEBUGPROC)DebugCallback, nullptr);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	}

	if (!CreateAllShaders())
		return false;

	if (!mObject)
		nextObject();

	SetupTexturemaps();
	SetupScene();
	SetupCameras();
	SetupStereoRenderTargets();
	SetupCompanionWindow();

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Initialize Compositor. Returns true if the compositor was
//          successfully initialized, false otherwise.
//-----------------------------------------------------------------------------
bool CMainApplication::BInitCompositor()
{
	vr::EVRInitError peError = vr::VRInitError_None;

	if (!vr::VRCompositor())
	{
		printf("Compositor initialization failed. See log file for details\n");
		return false;
	}

	return true;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainApplication::Shutdown()
{
	if (m_pHMD)
	{
		vr::VR_Shutdown();
		m_pHMD = NULL;
	}

	if (m_pContext)
	{
		if (m_bDebugOpenGL)
		{
			glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_FALSE);
			glDebugMessageCallback(nullptr, nullptr);
		}
		glDeleteBuffers(1, &m_glSceneVertBuffer);

		if (m_unSceneProgramID)
		{
			glDeleteProgram(m_unSceneProgramID);
		}
		if (m_unControllerTransformProgramID)
		{
			glDeleteProgram(m_unControllerTransformProgramID);
		}
		if (m_unRenderModelProgramID)
		{
			glDeleteProgram(m_unRenderModelProgramID);
		}
		if (m_unCompanionWindowProgramID)
		{
			glDeleteProgram(m_unCompanionWindowProgramID);
		}

		glDeleteRenderbuffers(1, &leftEyeDesc.m_nDepthBufferId);
		glDeleteTextures(1, &leftEyeDesc.m_nRenderTextureId);
		glDeleteFramebuffers(1, &leftEyeDesc.m_nRenderFramebufferId);
		glDeleteTextures(1, &leftEyeDesc.m_nResolveTextureId);
		glDeleteFramebuffers(1, &leftEyeDesc.m_nResolveFramebufferId);

		glDeleteRenderbuffers(1, &rightEyeDesc.m_nDepthBufferId);
		glDeleteTextures(1, &rightEyeDesc.m_nRenderTextureId);
		glDeleteFramebuffers(1, &rightEyeDesc.m_nRenderFramebufferId);
		glDeleteTextures(1, &rightEyeDesc.m_nResolveTextureId);
		glDeleteFramebuffers(1, &rightEyeDesc.m_nResolveFramebufferId);

		if (m_unCompanionWindowVAO != 0)
		{
			glDeleteVertexArrays(1, &m_unCompanionWindowVAO);
		}
		if (m_unSceneVAO != 0)
		{
			glDeleteVertexArrays(1, &m_unSceneVAO);
		}
		if (m_unControllerVAO != 0)
		{
			glDeleteVertexArrays(1, &m_unControllerVAO);
		}
	}

	if (m_pCompanionWindow)
	{
		SDL_DestroyWindow(m_pCompanionWindow);
		m_pCompanionWindow = NULL;
	}


	SDL_Quit();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainApplication::HandleInput()
{
	SDL_Event sdlEvent;
	bool bRet = false;

	while (SDL_PollEvent(&sdlEvent) != 0)
	{
		if (sdlEvent.type == SDL_QUIT)
		{
			bRet = true;
		}
		else if (sdlEvent.type == SDL_KEYDOWN)
		{
			if (sdlEvent.key.keysym.sym == SDLK_ESCAPE
				|| sdlEvent.key.keysym.sym == SDLK_q)
			{
				bRet = true;
			}
			if (sdlEvent.key.keysym.sym == SDLK_c)
			{
				m_bShowCubes = !m_bShowCubes;
			}
		}
	}

	// Process SteamVR events
	vr::VREvent_t event;
	while (m_pHMD->PollNextEvent(&event, sizeof(event)))
	{
		ProcessVREvent(event);
	}

	// Process SteamVR action state
	// UpdateActionState is called each frame to update the state of the actions themselves. The application
	// controls which action sets are active with the provided array of VRActiveActionSet_t structs.
	vr::VRActiveActionSet_t actionSet = { 0 };
	actionSet.ulActionSet = m_actionsetDemo;
	vr::VRInput()->UpdateActionState(&actionSet, sizeof(actionSet), 1);

	m_bShowCubes = !GetDigitalActionState(m_actionHideCubes);

	vr::VRInputValueHandle_t ulHapticDevice;
	if (GetDigitalActionRisingEdge(m_actionTriggerHaptic, &ulHapticDevice))
	{
		if (ulHapticDevice == m_rHand[Left].m_source)
		{
			vr::VRInput()->TriggerHapticVibrationAction(m_rHand[Left].m_actionHaptic, 0, 1, 4.f, 1.0f, vr::k_ulInvalidInputValueHandle);
		}
		if (ulHapticDevice == m_rHand[Right].m_source)
		{
			vr::VRInput()->TriggerHapticVibrationAction(m_rHand[Right].m_actionHaptic, 0, 1, 4.f, 1.0f, vr::k_ulInvalidInputValueHandle);
		}
	}

	vr::InputAnalogActionData_t analogData;
	if (vr::VRInput()->GetAnalogActionData(m_actionAnalongInput, &analogData, sizeof(analogData), vr::k_ulInvalidInputValueHandle) == vr::VRInputError_None && analogData.bActive)
	{
		m_vAnalogValue[0] = analogData.x;
		m_vAnalogValue[1] = analogData.y;
	}

	m_rHand[Left].m_bShowController = true;
	m_rHand[Right].m_bShowController = true;

	vr::VRInputValueHandle_t ulHideDevice;
	if (GetDigitalActionState(m_actionHideThisController, &ulHideDevice))
	{
		if (ulHideDevice == m_rHand[Left].m_source)
		{
			m_rHand[Left].m_bShowController = false;
		}
		if (ulHideDevice == m_rHand[Right].m_source)
		{
			m_rHand[Right].m_bShowController = false;
		}
	}

	return bRet;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainApplication::RunMainLoop()
{
	bool bQuit = false;

	SDL_StartTextInput();
	SDL_ShowCursor(SDL_ENABLE);

	while (!bQuit)
	{
		bQuit = HandleInput();

		if(!mObject)
			nextObject();

		RenderFrame();

//		auto time_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()) - start_time;
//
//		auto lines = std::vector<float>();
//		lines.push_back(0 - time_elapsed.count() * 0.0005);
//		lines.push_back(0 + time_elapsed.count() * 0.0005);
//		lines.push_back(0);
//		lines.push_back(-0.5 - time_elapsed.count() * 0.0005);
//		lines.push_back(0.5 + time_elapsed.count() * 0.0005);
//		lines.push_back(0);
	}

	SDL_StopTextInput();
}


//-----------------------------------------------------------------------------
// Purpose: Processes a single VR event
//-----------------------------------------------------------------------------
void CMainApplication::ProcessVREvent(const vr::VREvent_t& event)
{
	switch (event.eventType)
	{
	case vr::VREvent_TrackedDeviceDeactivated:
	{
		dprintf("Device %u detached.\n", event.trackedDeviceIndex);
	}
	break;
	case vr::VREvent_TrackedDeviceUpdated:
	{
		dprintf("Device %u updated.\n", event.trackedDeviceIndex);
	}
	break;
	}
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainApplication::RenderFrame()
{
	// for now as fast as possible
	if (m_pHMD)
	{
		RenderStereoTargets();
		RenderCompanionWindow();

		vr::Texture_t leftEyeTexture = { (void*)(uintptr_t)leftEyeDesc.m_nResolveTextureId, vr::TextureType_OpenGL, vr::ColorSpace_Gamma };
		vr::VRCompositor()->Submit(vr::Eye_Left, &leftEyeTexture);
		vr::Texture_t rightEyeTexture = { (void*)(uintptr_t)rightEyeDesc.m_nResolveTextureId, vr::TextureType_OpenGL, vr::ColorSpace_Gamma };
		vr::VRCompositor()->Submit(vr::Eye_Right, &rightEyeTexture);
	}

	if (m_bVblank && m_bGlFinishHack)
	{
		//$ HACKHACK. From gpuview profiling, it looks like there is a bug where two renders and a present
		// happen right before and after the vsync causing all kinds of jittering issues. This glFinish()
		// appears to clear that up. Temporary fix while I try to get nvidia to investigate this problem.
		// 1/29/2014 mikesart
		glFinish();
	}

	// SwapWindow
	{
		SDL_GL_SwapWindow(m_pCompanionWindow);
	}

	// Clear
	{
		// We want to make sure the glFinish waits for the entire present to complete, not just the submission
		// of the command. So, we do a clear here right here so the glFinish will wait fully for the swap.
		glClearColor(0.2, 0.2, 0.2, 1);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	// Flush and wait for swap.
	if (m_bVblank)
	{
		glFlush();
		glFinish();
	}

	// Spew out the controller and pose count whenever they change.
	if (m_iTrackedControllerCount != m_iTrackedControllerCount_Last || m_iValidPoseCount != m_iValidPoseCount_Last)
	{
		m_iValidPoseCount_Last = m_iValidPoseCount;
		m_iTrackedControllerCount_Last = m_iTrackedControllerCount;

		dprintf("PoseCount:%d(%s) Controllers:%d\n", m_iValidPoseCount, m_strPoseClasses.c_str(), m_iTrackedControllerCount);
	}

	UpdateHMDMatrixPose();
}

//-----------------------------------------------------------------------------
// Purpose: Compiles a GL shader program and returns the handle. Returns 0 if
//			the shader couldn't be compiled for some reason.
//-----------------------------------------------------------------------------
GLuint CMainApplication::CompileGLShader(const char* pchShaderName, const char* pchVertexShader, const char* pchFragmentShader)
{
	GLuint unProgramID = glCreateProgram();

	GLuint nSceneVertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(nSceneVertexShader, 1, &pchVertexShader, NULL);
	glCompileShader(nSceneVertexShader);

	GLint vShaderCompiled = GL_FALSE;
	glGetShaderiv(nSceneVertexShader, GL_COMPILE_STATUS, &vShaderCompiled);
	if (vShaderCompiled != GL_TRUE)
	{
		dprintf("%s - Unable to compile vertex shader %d!\n", pchShaderName, nSceneVertexShader);
		glDeleteProgram(unProgramID);
		glDeleteShader(nSceneVertexShader);
		return 0;
	}
	glAttachShader(unProgramID, nSceneVertexShader);
	glDeleteShader(nSceneVertexShader); // the program hangs onto this once it's attached

	GLuint  nSceneFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(nSceneFragmentShader, 1, &pchFragmentShader, NULL);
	glCompileShader(nSceneFragmentShader);

	GLint fShaderCompiled = GL_FALSE;
	glGetShaderiv(nSceneFragmentShader, GL_COMPILE_STATUS, &fShaderCompiled);
	if (fShaderCompiled != GL_TRUE)
	{
		dprintf("%s - Unable to compile fragment shader %d!\n", pchShaderName, nSceneFragmentShader);
		glDeleteProgram(unProgramID);
		glDeleteShader(nSceneFragmentShader);
		return 0;
	}

	glAttachShader(unProgramID, nSceneFragmentShader);
	glDeleteShader(nSceneFragmentShader); // the program hangs onto this once it's attached

	glLinkProgram(unProgramID);

	GLint programSuccess = GL_TRUE;
	glGetProgramiv(unProgramID, GL_LINK_STATUS, &programSuccess);
	if (programSuccess != GL_TRUE)
	{
		dprintf("%s - Error linking program %d!\n", pchShaderName, unProgramID);
		glDeleteProgram(unProgramID);
		return 0;
	}

	glUseProgram(unProgramID);
	glUseProgram(0);

	return unProgramID;
}


//-----------------------------------------------------------------------------
// Purpose: Creates all the shaders used by HelloVR SDL
//-----------------------------------------------------------------------------
bool CMainApplication::CreateAllShaders()
{
	m_unSceneProgramID = CompileGLShader(
		"Scene",

		// Vertex Shader
		"#version 410\n"
		"uniform mat4 matrix;\n"
		"layout(location = 0) in vec4 position;\n"
		"layout(location = 1) in vec2 v2UVcoordsIn;\n"
		"layout(location = 2) in vec3 v3NormalIn;\n"
		"out vec2 v2UVcoords;\n"
		"void main()\n"
		"{\n"
		"	v2UVcoords = v2UVcoordsIn;\n"
		"	gl_Position = matrix * position;\n"
		"}\n",

		// Fragment Shader
		"#version 410 core\n"
		"uniform sampler2D mytexture;\n"
		"in vec2 v2UVcoords;\n"
		"out vec4 outputColor;\n"
		"void main()\n"
		"{\n"
		"   outputColor = texture(mytexture, v2UVcoords);\n"
		"}\n"
	);
	m_nSceneMatrixLocation = glGetUniformLocation(m_unSceneProgramID, "matrix");
	if (m_nSceneMatrixLocation == -1)
	{
		dprintf("Unable to find matrix uniform in scene shader\n");
		return false;
	}

	m_unControllerTransformProgramID = CompileGLShader(
		"Controller",

		// vertex shader
		"#version 410\n"
		"uniform mat4 matrix;\n"
		"layout(location = 0) in vec4 position;\n"
		"layout(location = 1) in vec3 v3ColorIn;\n"
		"out vec4 v4Color;\n"
		"void main()\n"
		"{\n"
		"	v4Color.xyz = v3ColorIn; v4Color.a = 1.0;\n"
		"	gl_Position = matrix * position;\n"
		"}\n",

		// fragment shader
		"#version 410\n"
		"in vec4 v4Color;\n"
		"out vec4 outputColor;\n"
		"void main()\n"
		"{\n"
		"   outputColor = v4Color;\n"
		"}\n"
	);
	m_nControllerMatrixLocation = glGetUniformLocation(m_unControllerTransformProgramID, "matrix");
	if (m_nControllerMatrixLocation == -1)
	{
		dprintf("Unable to find matrix uniform in controller shader\n");
		return false;
	}

	m_unRenderModelProgramID = CompileGLShader(
		"render model",

		// vertex shader
		"#version 410\n"
		"uniform mat4 matrix;\n"
		"layout(location = 0) in vec4 position;\n"
		"layout(location = 1) in vec3 v3NormalIn;\n"
		"layout(location = 2) in vec2 v2TexCoordsIn;\n"
		"out vec2 v2TexCoord;\n"
		"void main()\n"
		"{\n"
		"	v2TexCoord = v2TexCoordsIn;\n"
		"	gl_Position = matrix * vec4(position.xyz, 1);\n"
		"}\n",

		//fragment shader
		"#version 410 core\n"
		"uniform sampler2D diffuse;\n"
		"in vec2 v2TexCoord;\n"
		"out vec4 outputColor;\n"
		"void main()\n"
		"{\n"
		"   outputColor = texture( diffuse, v2TexCoord);\n"
		"}\n"

	);
	m_nRenderModelMatrixLocation = glGetUniformLocation(m_unRenderModelProgramID, "matrix");
	if (m_nRenderModelMatrixLocation == -1)
	{
		dprintf("Unable to find matrix uniform in render model shader\n");
		return false;
	}

	m_unCompanionWindowProgramID = CompileGLShader(
		"CompanionWindow",

		// vertex shader
		"#version 410 core\n"
		"layout(location = 0) in vec4 position;\n"
		"layout(location = 1) in vec2 v2UVIn;\n"
		"noperspective out vec2 v2UV;\n"
		"void main()\n"
		"{\n"
		"	v2UV = v2UVIn;\n"
		"	gl_Position = position;\n"
		"}\n",

		// fragment shader
		"#version 410 core\n"
		"uniform sampler2D mytexture;\n"
		"noperspective in vec2 v2UV;\n"
		"out vec4 outputColor;\n"
		"void main()\n"
		"{\n"
		"		outputColor = texture(mytexture, v2UV);\n"
		"}\n"
	);

	m_unTestProgramID = CompileGLShader(
		"test",
		"#version 410\n"
		"uniform mat4 matrix;\n"
		"layout(location = 0) in vec4 position;\n"
		"void main()\n"
		"{\n"
		"	gl_Position = matrix * position;\n"
		"}\n",

		"#version 410\n"
		"out vec4 outputColor;\n"
		"void main()\n"
		"{\n"
		"        outputColor = vec4(0.0, 0.0, 1.0, 1.0);\n"
		"}\n"
		);
	m_nTestMatrixLocation = glGetUniformLocation(m_unTestProgramID, "matrix");
	if (m_nTestMatrixLocation == -1)
	{
		dprintf("Unable to find matrix uniform in render model shader\n");
		return false;
	}

	return m_unSceneProgramID != 0
		&& m_unControllerTransformProgramID != 0
		&& m_unRenderModelProgramID != 0
		&& m_unCompanionWindowProgramID != 0
		&& m_unTestProgramID != 0;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void AddCubeVertex(float fl0, float fl1, float fl2, float fl3, float fl4, std::vector<float>& vertdata)
{
	vertdata.push_back(fl0);
	vertdata.push_back(fl1);
	vertdata.push_back(fl2);
	vertdata.push_back(fl3);
	vertdata.push_back(fl4);
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void AddCubeToScene(Matrix4 mat, std::vector<float>& vertdata)
{
	// Matrix4 mat( outermat.data() );

	Vector4 A = mat * Vector4(0, 0, 0, 1);
	Vector4 B = mat * Vector4(1, 0, 0, 1);
	Vector4 C = mat * Vector4(1, 1, 0, 1);
	Vector4 D = mat * Vector4(0, 1, 0, 1);
	Vector4 E = mat * Vector4(0, 0, 1, 1);
	Vector4 F = mat * Vector4(1, 0, 1, 1);
	Vector4 G = mat * Vector4(1, 1, 1, 1);
	Vector4 H = mat * Vector4(0, 1, 1, 1);

	// triangles instead of quads
	AddCubeVertex(E.x, E.y, E.z, 0, 1, vertdata); //Front
	AddCubeVertex(F.x, F.y, F.z, 1, 1, vertdata);
	AddCubeVertex(G.x, G.y, G.z, 1, 0, vertdata);
	AddCubeVertex(G.x, G.y, G.z, 1, 0, vertdata);
	AddCubeVertex(H.x, H.y, H.z, 0, 0, vertdata);
	AddCubeVertex(E.x, E.y, E.z, 0, 1, vertdata);
	std::cout << E.x << std::endl;

	AddCubeVertex(B.x, B.y, B.z, 0, 1, vertdata); //Back
	AddCubeVertex(A.x, A.y, A.z, 1, 1, vertdata);
	AddCubeVertex(D.x, D.y, D.z, 1, 0, vertdata);
	AddCubeVertex(D.x, D.y, D.z, 1, 0, vertdata);
	AddCubeVertex(C.x, C.y, C.z, 0, 0, vertdata);
	AddCubeVertex(B.x, B.y, B.z, 0, 1, vertdata);

	AddCubeVertex(H.x, H.y, H.z, 0, 1, vertdata); //Top
	AddCubeVertex(G.x, G.y, G.z, 1, 1, vertdata);
	AddCubeVertex(C.x, C.y, C.z, 1, 0, vertdata);
	AddCubeVertex(C.x, C.y, C.z, 1, 0, vertdata);
	AddCubeVertex(D.x, D.y, D.z, 0, 0, vertdata);
	AddCubeVertex(H.x, H.y, H.z, 0, 1, vertdata);

	AddCubeVertex(A.x, A.y, A.z, 0, 1, vertdata); //Bottom
	AddCubeVertex(B.x, B.y, B.z, 1, 1, vertdata);
	AddCubeVertex(F.x, F.y, F.z, 1, 0, vertdata);
	AddCubeVertex(F.x, F.y, F.z, 1, 0, vertdata);
	AddCubeVertex(E.x, E.y, E.z, 0, 0, vertdata);
	AddCubeVertex(A.x, A.y, A.z, 0, 1, vertdata);

	AddCubeVertex(A.x, A.y, A.z, 0, 1, vertdata); //Left
	AddCubeVertex(E.x, E.y, E.z, 1, 1, vertdata);
	AddCubeVertex(H.x, H.y, H.z, 1, 0, vertdata);
	AddCubeVertex(H.x, H.y, H.z, 1, 0, vertdata);
	AddCubeVertex(D.x, D.y, D.z, 0, 0, vertdata);
	AddCubeVertex(A.x, A.y, A.z, 0, 1, vertdata);

	AddCubeVertex(F.x, F.y, F.z, 0, 1, vertdata); //Right
	AddCubeVertex(B.x, B.y, B.z, 1, 1, vertdata);
	AddCubeVertex(C.x, C.y, C.z, 1, 0, vertdata);
	AddCubeVertex(C.x, C.y, C.z, 1, 0, vertdata);
	AddCubeVertex(G.x, G.y, G.z, 0, 0, vertdata);
	AddCubeVertex(F.x, F.y, F.z, 0, 1, vertdata);
}


//-----------------------------------------------------------------------------
// Purpose: create a sea of cubes
//-----------------------------------------------------------------------------
void CMainApplication::SetupScene()
{
	if (!m_pHMD)
		return;

	std::vector<float> vertdataarray;

	// XXX/bmoody Review this function
	Matrix4 matScale;
	matScale.scale(m_fScale, m_fScale, m_fScale);
	Matrix4 matTransform;
	matTransform.translate(
		-((float)m_iSceneVolumeWidth * m_fScaleSpacing) / 2.f,
		-((float)m_iSceneVolumeHeight * m_fScaleSpacing) / 2.f,
		-((float)m_iSceneVolumeDepth * m_fScaleSpacing) / 2.f);

	Matrix4 mat = matScale * matTransform;

	for (int z = 0; z < m_iSceneVolumeDepth; z++)
	{
		for (int y = 0; y < m_iSceneVolumeHeight; y++)
		{
			for (int x = 0; x < m_iSceneVolumeWidth; x++)
			{
				AddCubeToScene( mat, vertdataarray );
				mat = mat * Matrix4().translate(m_fScaleSpacing, 0, 0);
			}
			mat = mat * Matrix4().translate(-((float)m_iSceneVolumeWidth) * m_fScaleSpacing, m_fScaleSpacing, 0);
		}
		mat = mat * Matrix4().translate(0, -((float)m_iSceneVolumeHeight) * m_fScaleSpacing, m_fScaleSpacing);
	}
	m_uiVertcount = vertdataarray.size() / 5;

	glGenVertexArrays(1, &m_unSceneVAO);
	glBindVertexArray(m_unSceneVAO);

	glGenBuffers(1, &m_glSceneVertBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, m_glSceneVertBuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * vertdataarray.size(), &vertdataarray[0], GL_STATIC_DRAW);

	GLsizei stride = sizeof(VertexDataScene);
	uintptr_t offset = 0;

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void*)offset);

	offset += sizeof(Vector3);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (const void*)offset);

	glBindVertexArray(0);
	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);

	// Add the test buffers
	glGenVertexArrays(1, &m_unTestVAO);
	glGenBuffers(1, &m_glTestBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, m_glTestBuffer);
	glBindVertexArray(0);
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainApplication::SetupCameras()
{
	m_mat4ProjectionLeft = GetHMDMatrixProjectionEye(vr::Eye_Left);
	m_mat4ProjectionRight = GetHMDMatrixProjectionEye(vr::Eye_Right);
	m_mat4eyePosLeft = GetHMDMatrixPoseEye(vr::Eye_Left);
	m_mat4eyePosRight = GetHMDMatrixPoseEye(vr::Eye_Right);
}


//-----------------------------------------------------------------------------
// Purpose: Creates a frame buffer. Returns true if the buffer was set up.
//          Returns false if the setup failed.
//-----------------------------------------------------------------------------
bool CMainApplication::CreateFrameBuffer(int nWidth, int nHeight, FramebufferDesc& framebufferDesc)
{
	glGenFramebuffers(1, &framebufferDesc.m_nRenderFramebufferId);
	glBindFramebuffer(GL_FRAMEBUFFER, framebufferDesc.m_nRenderFramebufferId);

	glGenRenderbuffers(1, &framebufferDesc.m_nDepthBufferId);
	glBindRenderbuffer(GL_RENDERBUFFER, framebufferDesc.m_nDepthBufferId);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT, nWidth, nHeight);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, framebufferDesc.m_nDepthBufferId);

	glGenTextures(1, &framebufferDesc.m_nRenderTextureId);
	glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, framebufferDesc.m_nRenderTextureId);
	glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA8, nWidth, nHeight, true);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, framebufferDesc.m_nRenderTextureId, 0);

	glGenFramebuffers(1, &framebufferDesc.m_nResolveFramebufferId);
	glBindFramebuffer(GL_FRAMEBUFFER, framebufferDesc.m_nResolveFramebufferId);

	glGenTextures(1, &framebufferDesc.m_nResolveTextureId);
	glBindTexture(GL_TEXTURE_2D, framebufferDesc.m_nResolveTextureId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, nWidth, nHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, framebufferDesc.m_nResolveTextureId, 0);

	// check FBO status
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		return false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	return true;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainApplication::SetupStereoRenderTargets()
{
	if (!m_pHMD)
		return false;

	m_pHMD->GetRecommendedRenderTargetSize(&m_nRenderWidth, &m_nRenderHeight);

	CreateFrameBuffer(m_nRenderWidth, m_nRenderHeight, leftEyeDesc);
	CreateFrameBuffer(m_nRenderWidth, m_nRenderHeight, rightEyeDesc);

	return true;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainApplication::SetupCompanionWindow()
{
	if (!m_pHMD)
		return;

	std::vector<VertexDataWindow> vVerts;

	// left eye verts
	vVerts.push_back(VertexDataWindow(Vector2(-1, -1), Vector2(0, 1)));
	vVerts.push_back(VertexDataWindow(Vector2(0, -1), Vector2(1, 1)));
	vVerts.push_back(VertexDataWindow(Vector2(-1, 1), Vector2(0, 0)));
	vVerts.push_back(VertexDataWindow(Vector2(0, 1), Vector2(1, 0)));

	// right eye verts
	vVerts.push_back(VertexDataWindow(Vector2(0, -1), Vector2(0, 1)));
	vVerts.push_back(VertexDataWindow(Vector2(1, -1), Vector2(1, 1)));
	vVerts.push_back(VertexDataWindow(Vector2(0, 1), Vector2(0, 0)));
	vVerts.push_back(VertexDataWindow(Vector2(1, 1), Vector2(1, 0)));

	GLushort vIndices[] = { 0, 1, 3,   0, 3, 2,   4, 5, 7,   4, 7, 6 };
	m_uiCompanionWindowIndexSize = _countof(vIndices);

	glGenVertexArrays(1, &m_unCompanionWindowVAO);
	glBindVertexArray(m_unCompanionWindowVAO);

	glGenBuffers(1, &m_glCompanionWindowIDVertBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, m_glCompanionWindowIDVertBuffer);
	glBufferData(GL_ARRAY_BUFFER, vVerts.size() * sizeof(VertexDataWindow), &vVerts[0], GL_STATIC_DRAW);

	glGenBuffers(1, &m_glCompanionWindowIDIndexBuffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_glCompanionWindowIDIndexBuffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_uiCompanionWindowIndexSize * sizeof(GLushort), &vIndices[0], GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(VertexDataWindow), (void*)offsetof(VertexDataWindow, position));

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(VertexDataWindow), (void*)offsetof(VertexDataWindow, texCoord));

	glBindVertexArray(0);

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainApplication::RenderStereoTargets()
{
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glEnable(GL_MULTISAMPLE);

	// Left Eye
	glBindFramebuffer(GL_FRAMEBUFFER, leftEyeDesc.m_nRenderFramebufferId);
	glViewport(0, 0, m_nRenderWidth, m_nRenderHeight);
	RenderScene(vr::Eye_Left);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glDisable(GL_MULTISAMPLE);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, leftEyeDesc.m_nRenderFramebufferId);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, leftEyeDesc.m_nResolveFramebufferId);

	glBlitFramebuffer(0, 0, m_nRenderWidth, m_nRenderHeight, 0, 0, m_nRenderWidth, m_nRenderHeight,
		GL_COLOR_BUFFER_BIT,
		GL_LINEAR);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

	glEnable(GL_MULTISAMPLE);

	// Right Eye
	glBindFramebuffer(GL_FRAMEBUFFER, rightEyeDesc.m_nRenderFramebufferId);
	glViewport(0, 0, m_nRenderWidth, m_nRenderHeight);
	RenderScene(vr::Eye_Right);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glDisable(GL_MULTISAMPLE);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, rightEyeDesc.m_nRenderFramebufferId);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, rightEyeDesc.m_nResolveFramebufferId);

	glBlitFramebuffer(0, 0, m_nRenderWidth, m_nRenderHeight, 0, 0, m_nRenderWidth, m_nRenderHeight,
		GL_COLOR_BUFFER_BIT,
		GL_LINEAR);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}


//-----------------------------------------------------------------------------
// Purpose: Renders a scene with respect to nEye.
//-----------------------------------------------------------------------------
void CMainApplication::RenderScene(vr::Hmd_Eye nEye)
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);

	bool bIsInputAvailable = m_pHMD->IsInputAvailable();

	// glUseProgram(m_unSceneProgramID);
	// glUniformMatrix4fv(m_nSceneMatrixLocation, 1, GL_FALSE, GetCurrentViewProjectionMatrix(nEye).get());
	// glBindVertexArray(m_unSceneVAO);
	// glBindTexture(GL_TEXTURE_2D, m_iTexture);
	// glDrawArrays(GL_TRIANGLES, 0, m_uiVertcount);
	// glBindVertexArray(0);
	// glUseProgram(0);

	glUseProgram(m_unTestProgramID);
	glUniformMatrix4fv(m_nTestMatrixLocation, 1, GL_FALSE, GetCurrentViewProjectionMatrix(nEye).get());

	for (auto& p : mPainter)
	{
		p->paint();
	}

	glUseProgram(0);


//	auto lines = get_lines();
//	m_numLines = lines.size();
//
//	std::vector<float> colors;
//	for (int i = 0; i < m_numLines; i++)
//	{
//		colors.push_back(0.0);
//		colors.push_back(0.0);
//		colors.push_back(1.0);
//	}
//	drawLines(lines, colors);
	

	// std::vector<float> points;
	// points.push_back(-0.5);
	// points.push_back(0.5);
	// points.push_back(0.0);
	// points.push_back(0.5);
	// points.push_back(0.5);
	// points.push_back(0.0);
	// points.push_back(0.5);
	// points.push_back(-0.5);
	// points.push_back(0.0);
	// points.push_back(-0.5);
	// points.push_back(-0.5);
	// points.push_back(0.0);
	// glUseProgram(m_unTestProgramID);
	// glEnableClientState(GL_VERTEX_ARRAY);
	// glVertexPointer(3, GL_FLOAT, 0, &points[0]);
	// glDrawArrays(GL_QUADS, 0, 4);
	// glDisableClientState(GL_VERTEX_ARRAY);
	// glUseProgram(0);
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainApplication::RenderCompanionWindow()
{
	glDisable(GL_DEPTH_TEST);
	glViewport(0, 0, m_nCompanionWindowWidth, m_nCompanionWindowHeight);

	glBindVertexArray(m_unCompanionWindowVAO);
	glUseProgram(m_unCompanionWindowProgramID);

	// render left eye (first half of index array )
	glBindTexture(GL_TEXTURE_2D, leftEyeDesc.m_nResolveTextureId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glDrawElements(GL_TRIANGLES, m_uiCompanionWindowIndexSize / 2, GL_UNSIGNED_SHORT, 0);

	// render right eye (second half of index array )
	glBindTexture(GL_TEXTURE_2D, rightEyeDesc.m_nResolveTextureId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glDrawElements(GL_TRIANGLES, m_uiCompanionWindowIndexSize / 2, GL_UNSIGNED_SHORT, (const void*)(uintptr_t)(m_uiCompanionWindowIndexSize));

	glBindVertexArray(0);
	glUseProgram(0);
}


//-----------------------------------------------------------------------------
// Purpose: Gets a Matrix Projection Eye with respect to nEye.
//-----------------------------------------------------------------------------
Matrix4 CMainApplication::GetHMDMatrixProjectionEye(vr::Hmd_Eye nEye)
{
	if (!m_pHMD)
		return Matrix4();

	vr::HmdMatrix44_t mat = m_pHMD->GetProjectionMatrix(nEye, m_fNearClip, m_fFarClip);

	return Matrix4(
		mat.m[0][0], mat.m[1][0], mat.m[2][0], mat.m[3][0],
		mat.m[0][1], mat.m[1][1], mat.m[2][1], mat.m[3][1],
		mat.m[0][2], mat.m[1][2], mat.m[2][2], mat.m[3][2],
		mat.m[0][3], mat.m[1][3], mat.m[2][3], mat.m[3][3]
	);
}


//-----------------------------------------------------------------------------
// Purpose: Gets an HMDMatrixPoseEye with respect to nEye.
//-----------------------------------------------------------------------------
Matrix4 CMainApplication::GetHMDMatrixPoseEye(vr::Hmd_Eye nEye)
{
	if (!m_pHMD)
		return Matrix4();

	vr::HmdMatrix34_t matEyeRight = m_pHMD->GetEyeToHeadTransform(nEye);
	Matrix4 matrixObj(
		matEyeRight.m[0][0], matEyeRight.m[1][0], matEyeRight.m[2][0], 0.0,
		matEyeRight.m[0][1], matEyeRight.m[1][1], matEyeRight.m[2][1], 0.0,
		matEyeRight.m[0][2], matEyeRight.m[1][2], matEyeRight.m[2][2], 0.0,
		matEyeRight.m[0][3], matEyeRight.m[1][3], matEyeRight.m[2][3], 1.0f
	);

	return matrixObj.invert();
}


//-----------------------------------------------------------------------------
// Purpose: Gets a Current View Projection Matrix with respect to nEye,
//          which may be an Eye_Left or an Eye_Right.
//-----------------------------------------------------------------------------
Matrix4 CMainApplication::GetCurrentViewProjectionMatrix(vr::Hmd_Eye nEye)
{
	Matrix4 matMVP;
	if (nEye == vr::Eye_Left)
	{
		matMVP = m_mat4ProjectionLeft * m_mat4eyePosLeft * m_mat4HMDPose;
	}
	else if (nEye == vr::Eye_Right)
	{
		matMVP = m_mat4ProjectionRight * m_mat4eyePosRight * m_mat4HMDPose;
	}

	return matMVP;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainApplication::UpdateHMDMatrixPose()
{
	if (!m_pHMD)
		return;

	vr::VRCompositor()->WaitGetPoses(m_rTrackedDevicePose, vr::k_unMaxTrackedDeviceCount, NULL, 0);

	m_iValidPoseCount = 0;
	m_strPoseClasses = "";
	for (int nDevice = 0; nDevice < vr::k_unMaxTrackedDeviceCount; ++nDevice)
	{
		if (m_rTrackedDevicePose[nDevice].bPoseIsValid)
		{
			m_iValidPoseCount++;
			m_rmat4DevicePose[nDevice] = ConvertSteamVRMatrixToMatrix4(m_rTrackedDevicePose[nDevice].mDeviceToAbsoluteTracking);
			if (m_rDevClassChar[nDevice] == 0)
			{
				switch (m_pHMD->GetTrackedDeviceClass(nDevice))
				{
				case vr::TrackedDeviceClass_Controller:        m_rDevClassChar[nDevice] = 'C'; break;
				case vr::TrackedDeviceClass_HMD:               m_rDevClassChar[nDevice] = 'H'; break;
				case vr::TrackedDeviceClass_Invalid:           m_rDevClassChar[nDevice] = 'I'; break;
				case vr::TrackedDeviceClass_GenericTracker:    m_rDevClassChar[nDevice] = 'G'; break;
				case vr::TrackedDeviceClass_TrackingReference: m_rDevClassChar[nDevice] = 'T'; break;
				default:                                       m_rDevClassChar[nDevice] = '?'; break;
				}
			}
			m_strPoseClasses += m_rDevClassChar[nDevice];
		}
	}

	if (m_rTrackedDevicePose[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid)
	{
		m_mat4HMDPose = m_rmat4DevicePose[vr::k_unTrackedDeviceIndex_Hmd];
		m_mat4HMDPose.invert();
	}
}


//-----------------------------------------------------------------------------
// Purpose: Converts a SteamVR matrix to our local matrix class
//-----------------------------------------------------------------------------
Matrix4 CMainApplication::ConvertSteamVRMatrixToMatrix4(const vr::HmdMatrix34_t& matPose)
{
	Matrix4 matrixObj(
		matPose.m[0][0], matPose.m[1][0], matPose.m[2][0], 0.0,
		matPose.m[0][1], matPose.m[1][1], matPose.m[2][1], 0.0,
		matPose.m[0][2], matPose.m[1][2], matPose.m[2][2], 0.0,
		matPose.m[0][3], matPose.m[1][3], matPose.m[2][3], 1.0f
	);
	return matrixObj;
}

}

#endif