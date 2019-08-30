/******************************************************************************
 *
 * MantaFlow fluid solver framework
 * Copyright 2011 Tobias Pfaff, Nils Thuerey 
 *
 * This program is free software, distributed under the terms of the
 * Apache License, Version 2.0 
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Base class for objects painting into the GL widget
 *
 ******************************************************************************/

#ifndef _PAINTER_H_
#define _PAINTER_H_

#include <map>
#include "grid.h"
#include "glrenderer.h"

namespace Manta {
	
// forward decl.
class PbClass;
class Mesh;

//! Base class for all painter
/*! Derived classes have to implement paint, doEvent */
class Painter {
public:
	enum PainterEvent { 
		EventNone = 0, UpdateRequest, UpdateFull, UpdateStep,
		EventScaleVecUpSm, EventScaleVecDownSm, EventScaleVecUp, EventScaleVecDown, 
		EventNextRealDisplayMode, EventScaleRealUp, EventScaleRealDown, EventScaleRealUpSm, EventScaleRealDownSm, EventChangePlane, 
		EventSetPlane, EventSetDim, EventNextInt, EventNextReal, EventNextVec, EventNextVecDisplayMode,
		EventNextMesh, EventMeshMode, EventToggleGridDisplay, EventScaleMeshUp, EventScaleMeshDown, EventMeshColorMode,
		EventNextSystem, EventToggleParticles, EventNextParticleDisplayMode, EventToggleBackgroundMesh, EventSetMax,
		EventScalePdataDown, EventScalePdataUp };

	//! display modes, note - 0=off,1=std are shared for real & vec grids! same semantics
	enum RealDisplayModes { RealDispOff=0, RealDispStd, RealDispLevelset, RealDispShadeVol, RealDispShadeSurf, NumRealDispModes }; 
	enum VecDisplayModes  { VecDispOff=0, VecDispCentered, VecDispStaggered, VecDispUv, NumVecDispModes };
	
	Painter() : mGLRenderer(nullptr), mVertexArray(), mBuffer(), mGridSize(), mViewPortUpdated(false) {}
	virtual ~Painter() {}
	
	std::string clickLine(const Vec3& p0, const Vec3& p1) { return ""; }
	void attachGLRenderer(glRenderer* renderer) { mGLRenderer = renderer; std::cout << "Attached rednerer " << mGLRenderer << std::endl; }

	virtual void setBackgroundMesh(Mesh* bgr) {}
	Vec3i getGridSize() {
		return mGridSize;
	}

	bool isViewportUpdated() {
		bool updated = mViewPortUpdated;
		mViewPortUpdated = false;
		return updated;
	}

	unsigned int setupBuffer(int index) {
		if (!mVertexArray[index] || !mBuffer[index])
		{
			if (mGLRenderer && mGLRenderer->mInitialized)
				mGLRenderer->setupBuffer(&mVertexArray[index], &mBuffer[index]);
			std::cout << "fetched " << mVertexArray[index] << " " << mBuffer[index] << std::endl;
		}
		return mBuffer[index];
	}
	
	virtual void paint() = 0;
	virtual void doEvent(int e, int param=0) = 0;

protected:
	glRenderer* mGLRenderer;
	std::map<unsigned int, unsigned int> mVertexArray;
	std::map<unsigned int, unsigned int> mBuffer; //! openGL handle for vertex buffer

	Vec3i mGridSize;
	bool mViewPortUpdated;
};

//! Base clas for all painters that require access to a locked PbClass
/*! Derived classes have to implement paint, update, getID, processKeyEvent. doEvent is handled in this class */
class LockedObjPainter : public Painter {
public:
	LockedObjPainter() :
		Painter(), mRequestUpdate(false), mObject(NULL), mObjIndex(-1) {}

	void doEvent(int e, int param=0); // don't overload, use processKeyEvent and update instead
	
protected:
	void nextObject();
	virtual std::string getID() = 0;
	virtual void update() = 0;
	virtual void processKeyEvent(PainterEvent e, int param) = 0;

	void addVec(std::vector<float>& vertices, std::vector<float>& colors, Vec3 vertex, Vec3 color, float mod = 1.0) {
		vertices.push_back(vertex.x * mod);
		vertices.push_back(vertex.y * mod);
		vertices.push_back(vertex.z * mod);
		colors.push_back(color.x);
		colors.push_back(color.y);
		colors.push_back(color.z);
	}

	void addQuad(std::vector<float>& vertices, std::vector<float>& colors, Vec3 boxVertices[4], Vec3 color, float mod = 1.0)
	{
		// Iterate over the index values needed to make this quad
		// Triangle 1: 0, 1, 2
		// Triangle 2: 0, 2, 3
		for (int i : {0, 1, 2, 0, 2, 3})
		{
			vertices.push_back(boxVertices[i].x * mod);
			vertices.push_back(boxVertices[i].y * mod);
			vertices.push_back(boxVertices[i].z * mod);
			colors.push_back(color.x);
			colors.push_back(color.y);
			colors.push_back(color.z);
		}
	}

	void addBox(std::vector<float>& vertices, Vec3& p0, const Vec3& p1, const float mod) {
		const int box[24] = { 0,1,0,2,0,4,7,6,7,5,7,3,1,3,1,5,2,3,2,6,4,5,4,6 };
		for (int i = 0; i < 24; i++) {
			const int b = box[i];
			Vec3 v = Vec3((b & 1) ? p1.x : p0.x, (b & 2) ? p1.y : p0.y, (b & 4) ? p1.z : p0.z);
			vertices.push_back(v.x * mod);
			vertices.push_back(v.y * mod);
			vertices.push_back(v.z * mod);
		}
	}
	
	bool     mRequestUpdate;
	PbClass* mObject;
	int      mObjIndex;
};

//! Painter object for int,Real,Vec3 grids
template<class T>
class GridPainter : public LockedObjPainter {
public:
	GridPainter(FlagGrid** flags = NULL);
	~GridPainter();
	
	void paint();
	Grid<T>** getGridPtr() { return &mLocalGrid; }
	int getPlane() { return mPlane; }
	int getDim()   { return mDim; }
	int getMax()   { return mMax; }
	virtual std::string clickLine(const Vec3& p0, const Vec3& p1);

protected:
	std::string getID();
	Real getScale();
	void setScale(Real v);
	int  getDispMode();
	void setDispMode(int dm);
	void update();
	void updateText();
	void processKeyEvent(PainterEvent e, int param);
	void processSpecificKeyEvent(PainterEvent e, int param);
	//void paintGridLines(bool lines, bool box);
	
	Real        mMaxVal;       //! stats
	int         mDim, mPlane, mMax;
	Grid<T>*    mLocalGrid;    //! currently selected grid
	FlagGrid**  mFlags;        //! flag grid (can influence display of selected grid)
	bool        mHide;         //! hide all grids?
	bool        mHideLocal;    //! hide only this type?
	std::map< void*, int > mDispMode; //! display modes , for each object
	std::map< std::pair<void*, int>, Real> mValScale; //! scaling of values , per object and display mode
};

}

#endif
