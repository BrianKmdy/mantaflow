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

	struct GLVertex {
		Vec3 vertex;
		Vec4 color;
		Vec3 normal;
		float size;
	};

	glRenderer();

	void swap();
	virtual void genBuffers() = 0;
	virtual void loadBuffers(unsigned int index) = 0;

	void draw();
	virtual void drawPoints() = 0;
	virtual void drawLines() = 0;
	virtual void drawTriangles() = 0;
	virtual void drawNormalTriangles() = 0;

	void addVertex(int shapeIndex, Vec3& vertex, Vec4& color, float mod = 1.0);
	void addNormalVertex(int shapeIndex, Vec3& vertex, Vec4& color, Vec3& normal, float mod = 1.0);
	void addPoint(int shapeIndex, Vec3& vertex, Vec4& color, float size, float mod = 1.0);
	void addQuad(int shapeIndex, Vec3 boxVertices[4], Vec4& color, float mod = 1.0);
	void addBox(int shapeIndex, Vec3& p0, const Vec3& p1, Vec4 color, float mod = 1.0);

	bool mInitialized;
	bool mBufferFilled;

	std::mutex m_swapMutex;

protected:
	unsigned int m_activeIndex;
	std::vector<GLVertex> m_vertices[2][NumShapes];

	unsigned int m_vertexCount[NumShapes];
	unsigned int m_vertexArrays[NumShapes];
	unsigned int m_vertexBuffers[NumShapes];
};

}