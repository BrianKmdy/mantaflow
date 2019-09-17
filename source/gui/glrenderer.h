#pragma once

#include "vectorbase.h"
#include "vector4d.h"

#include <mutex>
#include <vector>

namespace Manta {

class glRenderer
{
public:
	enum shapes { ShapePoints = 0, ShapeLines, ShapeFlatTriangles, ShapeShadedTriangles, NumShapes };

	glRenderer();

	void swapBuffers();
	virtual void genBuffers() = 0;
	virtual void loadBuffers(unsigned int index) = 0;

	void draw();
	virtual void drawLines() = 0;
	virtual void drawTriangles() = 0;
	virtual void drawNormalTriangles() = 0;

	void addVec(Vec3 vertex, Vec3 color, float mod = 1.0);
	void addNormalVec(Vec3 vertex, Vec4 color, Vec3 normal, float mod = 1.0);
	void addQuad(Vec3 boxVertices[4], Vec3 color, float mod = 1.0);
	void addBox(Vec3& p0, const Vec3& p1, Vec3 color, float mod = 1.0);

	bool mInitialized;
	bool mBufferFilled;

	std::mutex m_swapMutex;

protected:
	unsigned int m_activeIndex;
	std::vector<float> m_vertices[2][NumShapes];
	std::vector<float> m_colors[2][NumShapes];
	std::vector<float> m_normals[2][NumShapes];

	unsigned int m_vertexCount[NumShapes];
	unsigned int m_vertexArrays[NumShapes];
	unsigned int m_buffers[NumShapes];
};

}