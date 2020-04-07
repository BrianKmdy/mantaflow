/******************************************************************************
 *
 * MantaFlow fluid solver framework
 * Copyright 2011 Tobias Pfaff, Nils Thuerey 
 *
 * This program is free software, distributed under the terms of the
 * Apache License, Version 2.0 
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * GUI extension from python
 *
 ******************************************************************************/

#ifndef _CUSTOMCTRL_H__
#define _CUSTOMCTRL_H__

#include "manta.h"

namespace Manta {

// fwd decl.
class Mesh;


//! GUI adapter class to call from Python
PYTHON() class Gui : public PbClass {
public:
	PYTHON() Gui();
	
	PYTHON() void setBackgroundMesh(Mesh* m);
	PYTHON() void show(bool twoD=false);
	PYTHON() void update();
	PYTHON() void pause();
	PYTHON() PbClass* addControl(PbType t);
	PYTHON() void screenshot(std::string filename);

	// control display upon startup
	PYTHON() void nextRealGrid();
	PYTHON() void nextVec3Grid();
	PYTHON() void nextParts();
	PYTHON() void nextPdata();
	PYTHON() void nextMesh();
	PYTHON() void nextVec3Display();
	PYTHON() void nextMeshDisplay();
	PYTHON() void nextPartDisplay(); 
	PYTHON() void toggleHideGrids();
	PYTHON() void setCamPos(float x, float y, float z);
	PYTHON() void setCamRot(float x, float y, float z);  
	PYTHON() void windowSize(int w, int h);
	
};
	
} // namespace

#endif

