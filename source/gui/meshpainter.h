/******************************************************************************
 *
 * MantaFlow fluid solver framework
 * Copyright 2011 Tobias Pfaff, Nils Thuerey 
 *
 * This program is free software, distributed under the terms of the
 * Apache License, Version 2.0 
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Painting mesh objects
 *
 ******************************************************************************/

#ifndef _MESHPAINTER_H_
#define _MESHPAINTER_H_

#include "painter.h"

namespace Manta {
// fwd decl
class Mesh;
	
//! Painter object for Meshes
class MeshPainter : public LockedObjPainter {
public:
	enum DisplayMode { ModeTrans=0, ModeLines, ModePoints, ModeFlatShade, ModeInvisible, Num_DisplayModes };
	enum BackgroundMode { BModeNormal=0, BModeTrans, BModeInvisible, Num_BackgroundModes };
	enum VorticityMode { VModeFull=0, VModeSmoothed, VModeDiff, VModeSmoke, VModeTex, VModeNone, Num_VorticityModes };
	
	MeshPainter();
	~MeshPainter();
	
	void paint();
	
protected:
	std::string getID();    
	void update();
	void updateText();
	void processKeyEvent(PainterEvent e, int param);
	void processSpecificKeyEvent(PainterEvent e, int param);
	void setupLights(bool specular);
	
	Real mColorScale;
	DisplayMode mMode;    
	VorticityMode mVorticityMode;
	BackgroundMode mBackgroundMode;    
	Mesh* mLocalMesh, *mBackground;
	QLabel* mInfo;
	bool mHide;    
};

class QMeshPainter : public QPainter, public MeshPainter {
	Q_OBJECT
public:
	QMeshPainter(QWidget* par = 0);
	~QMeshPainter();

	void attachWidget(QLayout* layout);

	void paint() {
		MeshPainter::paint();
	}

	void doEvent(int e, int param = 0) {
		MeshPainter::doEvent(e, param);
	}

public slots:
	void setBackgroundMesh(Mesh* bgr);
};
	
} // namespace

#endif