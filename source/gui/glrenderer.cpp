#include "glrenderer.h"

namespace Manta {

glRenderer::glRenderer()
	: mInitialized(false), mBufferFilled(false), m_vertices(), m_colors(), m_normals(), m_activeIndex(0), m_vertexCount(), m_vertexArrays(), m_buffers(), m_swapMutex()
{
}

void glRenderer::swapBuffers() {
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
		m_colors[index][i].clear();
		m_normals[index][i].clear();
	}
}

void glRenderer::draw() {
	if (!mInitialized)
		return;

	drawLines();
	drawTriangles();
	drawNormalTriangles();
}

void glRenderer::addVec(Vec3 vertex, Vec3 color, float mod) {
	m_vertices[m_activeIndex][ShapeLines].push_back(vertex.x * mod);
	m_vertices[m_activeIndex][ShapeLines].push_back(vertex.y * mod);
	m_vertices[m_activeIndex][ShapeLines].push_back(vertex.z * mod);
	m_colors[m_activeIndex][ShapeLines].push_back(color.x);
	m_colors[m_activeIndex][ShapeLines].push_back(color.y);
	m_colors[m_activeIndex][ShapeLines].push_back(color.z);
}

void glRenderer::addNormalVec(Vec3 vertex, Vec4 color, Vec3 normal, float mod) {
	m_vertices[m_activeIndex][ShapeShadedTriangles].push_back(vertex.x * mod);
	m_vertices[m_activeIndex][ShapeShadedTriangles].push_back(vertex.y * mod);
	m_vertices[m_activeIndex][ShapeShadedTriangles].push_back(vertex.z * mod);
	m_colors[m_activeIndex][ShapeShadedTriangles].push_back(color.x);
	m_colors[m_activeIndex][ShapeShadedTriangles].push_back(color.y);
	m_colors[m_activeIndex][ShapeShadedTriangles].push_back(color.z);
	m_colors[m_activeIndex][ShapeShadedTriangles].push_back(color.t);
	m_normals[m_activeIndex][ShapeShadedTriangles].push_back(normal.x);
	m_normals[m_activeIndex][ShapeShadedTriangles].push_back(normal.y);
	m_normals[m_activeIndex][ShapeShadedTriangles].push_back(normal.z);
}

void glRenderer::addQuad(Vec3 boxVertices[4], Vec3 color, float mod)
{
	// Iterate over the m_activeIndex values needed to make this quad
	// Triangle 1: 0, 1, 2
	// Triangle 2: 0, 2, 3
	for (int i : {0, 1, 2, 0, 2, 3})
	{
		m_vertices[m_activeIndex][ShapeFlatTriangles].push_back(boxVertices[i].x * mod);
		m_vertices[m_activeIndex][ShapeFlatTriangles].push_back(boxVertices[i].y * mod);
		m_vertices[m_activeIndex][ShapeFlatTriangles].push_back(boxVertices[i].z * mod);
		m_colors[m_activeIndex][ShapeFlatTriangles].push_back(color.x);
		m_colors[m_activeIndex][ShapeFlatTriangles].push_back(color.y);
		m_colors[m_activeIndex][ShapeFlatTriangles].push_back(color.z);
	}
}

void glRenderer::addBox(Vec3& p0, const Vec3& p1, Vec3 color, float mod) {
	const int box[24] = { 0,1,0,2,0,4,7,6,7,5,7,3,1,3,1,5,2,3,2,6,4,5,4,6 };
	for (int i = 0; i < 24; i++) {
		const int b = box[i];
		Vec3 v = Vec3((b & 1) ? p1.x : p0.x, (b & 2) ? p1.y : p0.y, (b & 4) ? p1.z : p0.z);
		m_vertices[m_activeIndex][ShapeLines].push_back(v.x * mod);
		m_vertices[m_activeIndex][ShapeLines].push_back(v.y * mod);
		m_vertices[m_activeIndex][ShapeLines].push_back(v.z * mod);
		m_colors[m_activeIndex][ShapeLines].push_back(color.x);
		m_colors[m_activeIndex][ShapeLines].push_back(color.y);
		m_colors[m_activeIndex][ShapeLines].push_back(color.z);
	}
}

}