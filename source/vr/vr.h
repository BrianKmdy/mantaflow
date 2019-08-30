#pragma once

//========= Copyright Valve Corporation ============//
#define VR
#ifdef VR

#include <SDL.h>
#include <GL/glew.h>
#include <SDL_opengl.h>
#if defined( OSX )
#include <Foundation/Foundation.h>
#include <AppKit/AppKit.h>
#include <OpenGL/glu.h>
// Apple's version of glut.h #undef's APIENTRY, redefine it
#define APIENTRY
#else
#include <GL/glu.h>
#endif
#include <stdio.h>
#include <string>
#include <cstdlib>

#include <openvr.h>

#include "shared/lodepng.h"
#include "shared/Matrices.h"
#include "shared/pathtools.h"

#include "glrenderer.h"
#include "painter.h"
#include "particlepainter.h"
#include "meshpainter.h"


#if defined(POSIX)
#include "unistd.h"
#endif

#ifndef _WIN32
#define APIENTRY
#endif

#ifndef _countof
#define _countof(x) (sizeof(x)/sizeof((x)[0]))
#endif

void ThreadSleep(unsigned long nMilliseconds);
static bool g_bPrintf = true;

namespace Manta {


class VRPainter {
public:
	VRPainter(Painter* painter) : mPainter(painter) {
	}

	~VRPainter() {
		delete mPainter;
	}

public:
	std::string clickLine(const Vec3& p0, const Vec3& p1) {
		if (mPainter)
			mPainter->clickLine(p0, p1);

		return "";
	}

	void attachGLRenderer(glRenderer* renderer) {
		if (mPainter)
			mPainter->attachGLRenderer(renderer);
	}

public:
	virtual void paint() {
		if (mPainter)
			mPainter->paint();
	}

	virtual void doEvent(int e, int param = 0) {
		if (mPainter)
		{
			mPainter->doEvent(e, param);
			//	if (mPainter->isViewportUpdated())
			//		emit setViewport(mPainter->getGridSize());
		}
	}

	void setBackgroundMesh(Mesh* bgr) {
		mPainter->setBackgroundMesh(bgr);
	}

private:
	Painter* mPainter;
};

//-----------------------------------------------------------------------------
// Purpose:
//------------------------------------------------------------------------------
class CMainApplication : public glRenderer
{
public:
	CMainApplication(int argc, char* argv[]);
	virtual ~CMainApplication();

	void setupBuffer(unsigned int* vertexArray, unsigned int* buffer);
	void drawLines(unsigned int vertexArray, unsigned int buffer, std::vector<float>& vertices, std::vector<float>& colors);
	void drawTriangles(unsigned int vertexArray, unsigned int buffer, std::vector<float>& vertices, std::vector<float>& colrs);

	bool BInit();
	bool BInitGL();
	bool BInitCompositor();

	void Shutdown();

	void RunMainLoop();
	bool HandleInput();
	void ProcessVREvent(const vr::VREvent_t& event);
	void RenderFrame();

	bool SetupTexturemaps();

	void SetupScene();

	bool SetupStereoRenderTargets();
	void SetupCompanionWindow();
	void SetupCameras();

	void RenderStereoTargets();
	void RenderCompanionWindow();
	void RenderScene(vr::Hmd_Eye nEye);

	Matrix4 GetHMDMatrixProjectionEye(vr::Hmd_Eye nEye);
	Matrix4 GetHMDMatrixPoseEye(vr::Hmd_Eye nEye);
	Matrix4 GetCurrentViewProjectionMatrix(vr::Hmd_Eye nEye);
	void UpdateHMDMatrixPose();

	Matrix4 ConvertSteamVRMatrixToMatrix4(const vr::HmdMatrix34_t& matPose);

	GLuint CompileGLShader(const char* pchShaderName, const char* pchVertexShader, const char* pchFragmentShader);
	bool CreateAllShaders();

	bool m_Initialized;

private:
	bool m_bDebugOpenGL;
	bool m_bVerbose;
	bool m_bPerf;
	bool m_bVblank;
	bool m_bGlFinishHack;

	vr::IVRSystem* m_pHMD;
	std::string m_strDriver;
	std::string m_strDisplay;
	vr::TrackedDevicePose_t m_rTrackedDevicePose[vr::k_unMaxTrackedDeviceCount];
	Matrix4 m_rmat4DevicePose[vr::k_unMaxTrackedDeviceCount];
	vr::Hmd_Eye m_currentEye;

	struct ControllerInfo_t
	{
		vr::VRInputValueHandle_t m_source = vr::k_ulInvalidInputValueHandle;
		vr::VRActionHandle_t m_actionPose = vr::k_ulInvalidActionHandle;
		vr::VRActionHandle_t m_actionHaptic = vr::k_ulInvalidActionHandle;
		Matrix4 m_rmat4Pose;
		std::string m_sRenderModelName;
		bool m_bShowController;
	};

	enum EHand
	{
		Left = 0,
		Right = 1,
	};
	ControllerInfo_t m_rHand[2];

private: // SDL bookkeeping
	SDL_Window* m_pCompanionWindow;
	uint32_t m_nCompanionWindowWidth;
	uint32_t m_nCompanionWindowHeight;

	SDL_GLContext m_pContext;
	std::vector<VRPainter*> mPainter;

private: // OpenGL bookkeeping
	int m_iTrackedControllerCount;
	int m_iTrackedControllerCount_Last;
	int m_iValidPoseCount;
	int m_iValidPoseCount_Last;
	bool m_bShowCubes;
	Vector2 m_vAnalogValue;

	std::string m_strPoseClasses;                            // what classes we saw poses for this frame
	char m_rDevClassChar[vr::k_unMaxTrackedDeviceCount];   // for each device, a character representing its class

	int m_iSceneVolumeWidth;
	int m_iSceneVolumeHeight;
	int m_iSceneVolumeDepth;
	float m_fScaleSpacing;
	float m_fScale;

	int m_iSceneVolumeInit;                                  // if you want something other than the default 20x20x20

	float m_fNearClip;
	float m_fFarClip;

	GLuint m_iTexture;

	unsigned int m_uiVertcount;

	GLuint m_glSceneVertBuffer;
	GLuint m_unSceneVAO;
	GLuint m_unCompanionWindowVAO;
	GLuint m_glCompanionWindowIDVertBuffer;
	GLuint m_glCompanionWindowIDIndexBuffer;
	unsigned int m_uiCompanionWindowIndexSize;

	GLuint m_glControllerVertBuffer;
	GLuint m_unControllerVAO;
	unsigned int m_uiControllerVertcount;

	GLuint m_glTestBuffer;
	GLuint m_unTestVAO;
	unsigned int m_testVertCount;
	unsigned int m_numLines;

	Matrix4 m_mat4HMDPose;
	Matrix4 m_mat4eyePosLeft;
	Matrix4 m_mat4eyePosRight;

	Matrix4 m_mat4ProjectionCenter;
	Matrix4 m_mat4ProjectionLeft;
	Matrix4 m_mat4ProjectionRight;

	struct VertexDataScene
	{
		Vector3 position;
		Vector2 texCoord;
	};

	struct VertexDataWindow
	{
		Vector2 position;
		Vector2 texCoord;

		VertexDataWindow(const Vector2& pos, const Vector2 tex) : position(pos), texCoord(tex) {	}
	};

	GLuint m_unSceneProgramID;
	GLuint m_unCompanionWindowProgramID;
	GLuint m_unControllerTransformProgramID;
	GLuint m_unRenderModelProgramID;
	GLuint m_unTestProgramID;

	GLint m_nSceneMatrixLocation;
	GLint m_nControllerMatrixLocation;
	GLint m_nRenderModelMatrixLocation;
	GLint m_nTestMatrixLocation;

	struct FramebufferDesc
	{
		GLuint m_nDepthBufferId;
		GLuint m_nRenderTextureId;
		GLuint m_nRenderFramebufferId;
		GLuint m_nResolveTextureId;
		GLuint m_nResolveFramebufferId;
	};
	FramebufferDesc leftEyeDesc;
	FramebufferDesc rightEyeDesc;

	bool CreateFrameBuffer(int nWidth, int nHeight, FramebufferDesc& framebufferDesc);

	uint32_t m_nRenderWidth;
	uint32_t m_nRenderHeight;

	vr::VRActionHandle_t m_actionHideCubes = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_actionHideThisController = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_actionTriggerHaptic = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_actionAnalongInput = vr::k_ulInvalidActionHandle;

	vr::VRActionSetHandle_t m_actionsetDemo = vr::k_ulInvalidActionSetHandle;
};

bool GetDigitalActionRisingEdge(vr::VRActionHandle_t action, vr::VRInputValueHandle_t* pDevicePath = nullptr);
bool GetDigitalActionFallingEdge(vr::VRActionHandle_t action, vr::VRInputValueHandle_t* pDevicePath = nullptr);
bool GetDigitalActionState(vr::VRActionHandle_t action, vr::VRInputValueHandle_t* pDevicePath = nullptr);
void dprintf(const char* fmt, ...);
std::string GetTrackedDeviceString(vr::TrackedDeviceIndex_t unDevice, vr::TrackedDeviceProperty prop, vr::TrackedPropertyError* peError = NULL);

}

#endif