#include "glrenderer.h"

namespace Manta {

glRenderer::glRenderer()
	: mInitialized(false), mBufferFilled(false), m_vertices(), m_activeIndex(0), m_vertexCount(), m_vertexArrays(), m_vertexBuffers(), m_swapMutex()
{
}

void glRenderer::swap() {
	if (!mInitialized)
		return;

	unsigned int index = m_activeIndex;

	{
		std::lock_guard<std::mutex> lock(m_swapMutex);

		m_activeIndex = (m_activeIndex + 1) % 2;
		mBufferFilled = false;
	}

	loadBuffers(index);

	for (unsigned int i = 0; i < NumShapes; ++i) {
		m_vertices[index][i].clear();
	}
}

void glRenderer::draw() {
	if (!mInitialized)
		return;

	drawPoints();
	drawLines();
	drawTriangles();
	drawNormalTriangles();
}

void glRenderer::addVertex(int shapeIndex, Vec3& vertex, Vec4& color, float mod)
{
	m_vertices[m_activeIndex][shapeIndex].push_back({ vertex * mod, color });
}

void glRenderer::addNormalVertex(int shapeIndex, Vec3& vertex, Vec4& color, Vec3& normal, float mod)
{
	// XXX/bmoody Review if this is correct for the normal
	m_vertices[m_activeIndex][shapeIndex].push_back({ vertex * mod, color, normal * mod });
}

void glRenderer::addPoint(int shapeIndex, Vec3& vertex, Vec4& color, float size, float mod)
{
	m_vertices[m_activeIndex][shapeIndex].push_back({ vertex * mod, color, Vec3(), size });
}

void glRenderer::addQuad(int shapeIndex, Vec3 boxVertices[4], Vec4& color, float mod)
{
	// Triangle 1: 0, 1, 2
	// Triangle 2: 0, 2, 3
	for (int i : {0, 1, 2, 0, 2, 3}) {
		m_vertices[m_activeIndex][shapeIndex].push_back({ boxVertices[i] * mod, color });
	}
}

void glRenderer::addBox(int shapeIndex, Vec3& p0, const Vec3& p1, Vec4 color, float mod)
{
	const int box[24] = { 0,1,0,2,0,4,7,6,7,5,7,3,1,3,1,5,2,3,2,6,4,5,4,6 };
	for (int i = 0; i < 24; i++) {
		const int b = box[i];
		Vec3 v = Vec3((b & 1) ? p1.x : p0.x, (b & 2) ? p1.y : p0.y, (b & 4) ? p1.z : p0.z);
		m_vertices[m_activeIndex][shapeIndex].push_back({ v * mod, color });
	}
}

}