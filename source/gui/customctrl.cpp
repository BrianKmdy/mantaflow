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

#include "customctrl.h"
#include "painter.h"
#include "general.h"

using namespace std;

namespace Manta {
	

// **************************************************************************************
// GUI class

Gui::Gui() :
	PbClass(NULL) {
	if (getenv("MANTA_DISABLE_UI") && atoi(getenv("MANTA_DISABLE_UI"))) { errMsg("Cannot create GUI object, GUI disabled"); }
}

void Gui::setBackgroundMesh(Mesh* m) {}
void Gui::show(bool twoD) {}
void Gui::update() {}
void Gui::pause() {}
void Gui::screenshot(string filename) {}
void Gui::nextRealGrid() {}
void Gui::nextVec3Grid() {}
void Gui::nextParts() {}
void Gui::nextPdata() {}
void Gui::nextMesh() {}
void Gui::nextVec3Display() {}
void Gui::nextPartDisplay() {}
void Gui::nextMeshDisplay() {}
void Gui::toggleHideGrids() {}
void Gui::setCamPos(float x, float y, float z) { }
void Gui::setCamRot(float x, float y, float z) {}
void Gui::windowSize(int w, int h) {}
PbClass* Gui::addControl(PbType t) {
	_args.add("nocheck", true);
	if (t.str() == "")
		throw Error("Need to specify object type. Use e.g. gui.create(Slider, ...)");

	PbClass* obj = PbClass::createPyObject(t.str(), "", _args, this);
	if (!obj || !obj->canConvertTo("CustomControl"))
		throw Error("gui.create() can only create CustomControl-based objects");

	return obj;
}


} // namespace
