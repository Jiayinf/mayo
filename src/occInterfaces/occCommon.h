#ifndef OCCCOMMON_H
#define OCCCOMMON_H

#pragma once

#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <STEPControl_Reader.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <Geom_Curve.hxx>
#include <BRep_Tool.hxx>
#include <TDF_Label.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <AIS_Trihedron.hxx>
#include <V3d_View.hxx>

#include "../graphics/graphics_scene.h"

//std::string occGetShapeName(const TDF_Label& label);
//bool occGetShapeColor(const TDF_Label& label, const Handle(XCAFDoc_ColorTool)& colorTool, float rgb[3]);
//bool occConvertShapeToGLData(const TDF_Label& label, const Handle(AIS_InteractiveObject) occObj, const Handle(XCAFDoc_ShapeTool)& shapeTool, const Handle(XCAFDoc_ColorTool)& colorTool, std::vector<std::vector<float>>& floatVectorParams, std::vector<std::vector<int>>& intVectorParams, float mat[12], int unit = 0);
//
//bool occImportEntity(const char* fileName);
////bool occConvertShapeToBrep(const TopoDS_Shape& shape, std::string shapeName, float* rgb, CBrep* brep, int unit = 0);
//int occGetFaceCentroidDummy(const TopoDS_Shape& shape, int unit = 0);
//int occGetCurveCentroidDummy(const TopoDS_Shape& shape, int unit = 0);
//
//bool occGetEntityMesh(int entityId, std::vector<std::vector<float>>& floatVectorParams, std::vector<std::vector<int>>& intVectorParams, float mat[12]);
//
//bool occCreateCylinder(TopoDS_Shape& shape, float pos[3], float dir[3], double radius, double height, std::string name = "Cylinder");
//bool occCreateBox(TopoDS_Shape& shape, float pos[3], float dir[3], float length, float width, float height);
//bool occCreateSphere(TopoDS_Shape& shape, float pos[3], float radius);
//
//bool occSetBrepRigidBody(int brepId, int rigidBodyId);
//
//bool occSetShapeLocation(TopoDS_Shape& shape, float c7vec[7], int unit = 0);
//
////void occSetBrepLocation(CBrep* brep, TopLoc_Location& location, int unit = 0);
//
//void occMatToTrsf(float mat[12], gp_Trsf& trsf, int unit = 0);
//void occDefaultTrsf(gp_Trsf& trsf);
void occLocToMat(TopLoc_Location& loc, float mat[12], int unit = 0);
void occMatToLoc(float mat[12], TopLoc_Location& loc, int unit = 0);
void occTrsfToMat(gp_Trsf& trsf, float mat[12], int unit = 0);
void occAx2ToMat(gp_Ax2& ax2, float mat[12], int unit = 0);
void occMatToAx2(float mat[12], gp_Ax2& ax2, int unit = 0);

//bool occGetFaceCentroidCsys(const TopoDS_Shape& shape, Handle(AIS_Trihedron)& csysTrihedron, float mat[12], gp_Pnt& centroid, int normalIndex, int unit = 1);
//bool occGetFaceUVParamCsys(const TopoDS_Shape& shape, Handle(AIS_Trihedron)& csysTrihedron, float mat[12], double u, double v, gp_Pnt pos, int unit = 1);
//bool occGetCenterofCurvatureCsys(const TopoDS_Shape& shape, Handle(AIS_Trihedron)& csysTrihedron, float mat[12], gp_Pnt& centerofCurvature);
//bool occFindLabelByShapeTag(const TDF_Label& rootLabel, const Standard_Integer shapeTag, TDF_Label& shapeLabel);
//
//bool occGetGfxObjectShape(Handle(AIS_InteractiveObject)& aisObj, TopoDS_Shape& shape);
//
//void occSetTrihedronDefaultStyle(Handle(AIS_Trihedron)& aisTrihedron);
//
//bool occCreateTrihedron(Handle(AIS_Trihedron)& csysTrihedron, float mat[12], int unit = 0);
////bool occCreatePointShape(Handle(AIS_Shape)& pointShape, float mat[12], int unit = 0);
//
//bool occCreateRevoluteJointShape(TopoDS_Shape& shape, std::vector<Quantity_Color>& vertexColors, float mat[12], int unit = 0);
//bool occCreatePrismaticJointShape(TopoDS_Shape& shape, std::vector<Quantity_Color>& vertexColors, float mat[12], int unit = 0);
//
//std::string occGetLabelEntry(const TDF_Label& label);
//TopoDS_Shape occGetShapeFromEntityOwner(const Handle(SelectMgr_EntityOwner)& owner);
//
//void occV3dviewRedraw(const Handle(V3d_View)& view, bool checkSimStopped = true);
//void occV3dviewSetScale(const Handle(V3d_View)& view, float radio, bool checkSimStopped = true);
//void occV3dviewPan(const Handle(V3d_View)& view, int dx, int dy, float theZoomFactor = 1, bool theToStart = true, bool checkSimStopped = true);
//void occV3dviewStartZoomAtPoint(const Handle(V3d_View)& view, int x, int y, bool checkSimStopped = true);
//void occV3dviewZoom(const Handle(V3d_View)& view, int x1, int y1, int x2, int y2, bool checkSimStopped = true);
//void occV3dviewZoomAtPoint(const Handle(V3d_View)& view, int x1, int y1, int x2, int y2, bool checkSimStopped = true);
//void occV3dviewStartRotation(const Handle(V3d_View)& view, int x, int y, bool checkSimStopped = true);
//void occV3dviewRotation(const Handle(V3d_View)& view, int x, int y, bool checkSimStopped = true);
//void occV3dviewWindowFitAll(const Handle(V3d_View)& view, int xMin, int yMin, int xMax, int yMax, bool checkSimStopped = true);
//void occV3dviewTurn(const Handle(V3d_View)& view, V3d_TypeOfAxe& Axe, Standard_Real Angle, Standard_Boolean Start = Standard_True, bool checkSimStopped = true);
//
//void occGraphicsSceneHighlightAt(Mayo::GraphicsScene* graphicsScene, int xPos, int yPos, const Handle_V3d_View& view, bool checkSimStopped = true);
//void occGraphicsScenePick(Mayo::GraphicsScene* graphicsScene, int xPos, int yPos, const Handle_V3d_View& view, bool checkSimStopped = true);
//void occGraphicsSceneAddObject(Mayo::GraphicsScene* graphicsScene, Handle(AIS_InteractiveObject) gfxObj, bool checkSimStopped = true);
//void occGraphicsSceneEraseObject(Mayo::GraphicsScene* graphicsScene, Handle(AIS_InteractiveObject) gfxObj, bool checkSimStopped = true);
//void occGraphicsSceneSetObjectSelect(Mayo::GraphicsScene* graphicsScene, Handle(AIS_InteractiveObject)& gfxObj, bool sel, bool checkSimStopped = true);
//void occGraphicsSceneSetObjectTransfrom(Mayo::GraphicsScene* graphicsScene, Handle(AIS_InteractiveObject)& gfxObj, const gp_Trsf& trsf, bool checkSimStopped = true);

#endif // !OCCCOMMON_H
