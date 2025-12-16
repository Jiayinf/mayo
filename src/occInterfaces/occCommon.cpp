#include "occCommon.h"
//#include "simInternalExt.h"
//#include "app.h"

#include "../base/brep_utils.h"
#include "../base/tkernel_utils.h"

#include <gp_Quaternion.hxx>
#include <gp_Vec.hxx>
#include <gp_Pnt.hxx>
#include <GCPnts_UniformAbscissa.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <TopLoc_Location.hxx>
#include <Poly_Array1OfTriangle.hxx>
#include <XCAFApp_Application.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <TDataStd_Name.hxx>
#include <Quantity_Color.hxx>
#include <XCAFDoc_Color.hxx>
#include <TDF_ChildIDIterator.hxx>
#include <TopExp.hxx>
#include <TopoDS_Vertex.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Line.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <BRepAdaptor_CompCurve.hxx>
#include <BRep_Tool.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepLProp_CLProps.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <AIS_ConnectedInteractive.hxx>
#include <Geom_Axis2Placement.hxx>
#include <BRepBndLib.hxx>
#include <XCAFPrs_AISObject.hxx>
#include <GeomLProp_CLProps.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <PrsDim_TypeOfAngle.hxx>
#include <GeomLProp_SLProps.hxx>
#include <GC_MakeCircle.hxx>
#include <gp_Circ.hxx>
#include <random>
#include <QMutex>


#if 0
std::string occGetShapeName(const TDF_Label& label)
{
    std::string shapeName = "shape";

    Handle(TDataStd_Name) aName;
    if (label.FindAttribute(TDataStd_Name::GetID(), aName))
    {
        char* tmpBuffer = new char[256] { 0 };
        aName->Get().ToUTF8CString(tmpBuffer);
        shapeName = tmpBuffer;
        delete[] tmpBuffer;
    }

    return shapeName;
}

void occGetShapeColor(const TDF_Label& label, const Handle(XCAFDoc_ColorTool)& colorTool, float rgb[3], float*& pRgb)
{
    if (colorTool->IsSet(label, XCAFDoc_ColorGen))
    {
        Quantity_Color aColor;
        colorTool->GetColor(label, aColor);

        rgb[0] = aColor.Red();
        rgb[1] = aColor.Green();
        rgb[2] = aColor.Blue();
        pRgb = rgb;
    }
}

bool occGetShapeColor(const TDF_Label& label, const Handle(XCAFDoc_ColorTool)& colorTool, float rgb[3])
{
    if (!colorTool)
    {
        return false;
    }

    Quantity_Color color = {};

    if (colorTool->GetColor(label, XCAFDoc_ColorGen, color))
    {
        rgb[0] = color.Red();
        rgb[1] = color.Green();
        rgb[2] = color.Blue();
    }
    else if (colorTool->GetColor(label, XCAFDoc_ColorSurf, color))
    {
        rgb[0] = color.Red();
        rgb[1] = color.Green();
        rgb[2] = color.Blue();
    }
    else if (colorTool->GetColor(label, XCAFDoc_ColorCurv, color))
    {
        rgb[0] = color.Red();
        rgb[1] = color.Green();
        rgb[2] = color.Blue();
    }
    else
    {
        std::default_random_engine e;
        std::uniform_real_distribution<double> random(0, 1);

        rgb[0] = random(e);
        rgb[1] = random(e);
        rgb[2] = random(e);

        return false;
    }

    return true;
}

void occTraverseBrepNode(CBrep* brep, std::vector<CBrep*>& breps)
{
    //if (NULL != brep->shape)
    //{
    //    breps.push_back(brep);
    //}

    //for (int i = 0; i < brep->childern.size(); i++)
    //{
    //    CBrep* childBrep = brep->childern.at(i);
    //    if (NULL != childBrep->shape)
    //    {
    //        breps.push_back(childBrep);
    //    }

    //    occTraverseBrepNode(childBrep, breps);
    //}
}

void occConvertShapeToGLData(CBrep* brep, std::vector<std::vector<float>>& floatVectorParams, std::vector<std::vector<int>>& intVectorParams, int unit = 0)
{
    //if ((NULL == brep) || (NULL == brep->shape))
    //{
    //    return;
    //}

    //int dividend = 1;

    //if (0 == unit)
    //{
    //    dividend = 1000;
    //}

    //std::vector<float> vertices;
    //std::vector<int> indices;

    ////int count = 0;
    ////TopExp_Explorer explorer(*(brep->shape), TopAbs_FACE);
    ////for (; explorer.More(); explorer.Next())
    ////{
    ////    TopoDS_Face face = TopoDS::Face(explorer.Current());
    ////    BRepMesh_IncrementalMesh(face, 0.01);

    ////    TopLoc_Location faceLocation;
    ////    Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, faceLocation);
    ////    if (!triangulation.IsNull())
    ////    {
    ////        int nsize = triangulation->NbNodes();
    ////        for (int i = 1; ((i + 2) <= nsize); i += 3)
    ////        {
    ////            const gp_Pnt& point = triangulation->Node(i).Transformed(faceLocation.Transformation());
    ////            vertices.push_back(point.X() / dividend);
    ////            vertices.push_back(point.Y() / dividend);
    ////            vertices.push_back(point.Z() / dividend);

    ////            const gp_Pnt& point1 = triangulation->Node(i + 1).Transformed(faceLocation.Transformation());
    ////            vertices.push_back(point1.X() / dividend);
    ////            vertices.push_back(point1.Y() / dividend);
    ////            vertices.push_back(point1.Z() / dividend);

    ////            const gp_Pnt& point2 = triangulation->Node(i + 2).Transformed(faceLocation.Transformation());
    ////            vertices.push_back(point2.X() / dividend);
    ////            vertices.push_back(point2.Y() / dividend);
    ////            vertices.push_back(point2.Z() / dividend);

    ////            indices.push_back(count++);
    ////            indices.push_back(count++);
    ////            indices.push_back(count++);
    ////        }
    ////    }
    ////}

    //int vertexOffset = 0;
    //TopExp_Explorer explorer(*(brep->shape), TopAbs_FACE);
    //for (; explorer.More(); explorer.Next()) 
    //{
    //    TopoDS_Face face = TopoDS::Face(explorer.Current());
    //    BRepMesh_IncrementalMesh(face, 0.01);

    //    TopLoc_Location faceLocation = face.Location();
    //    Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, faceLocation);
    //    if (!triangulation.IsNull())
    //    {
    //        int nsize = triangulation->NbNodes();
    //        for (int i = 1; i <= nsize; i++) 
    //        {
    //            const gp_Pnt& point = triangulation->Node(i).Transformed(faceLocation.Transformation());
    //            vertices.push_back(point.X() / dividend);
    //            vertices.push_back(point.Y() / dividend);
    //            vertices.push_back(point.Z() / dividend);
    //        }

    //        int tsize = triangulation->NbTriangles();
    //        for (int i = 1; i <= tsize; i++) 
    //        {
    //            const Poly_Triangle& triangle = triangulation->Triangle(i);
    //            int n1, n2, n3;
    //            triangle.Get(n1, n2, n3);
    //            indices.push_back(vertexOffset + n1 - 1);
    //            indices.push_back(vertexOffset + n2 - 1);
    //            indices.push_back(vertexOffset + n3 - 1);
    //        }

    //        vertexOffset += nsize;
    //    }
    //}

    //if ((indices.size() >= 3) && (vertices.size() >= 9))
    //{
    //    vertices.push_back(brep->rgb[0] * 255);
    //    vertices.push_back(brep->rgb[1] * 255);
    //    vertices.push_back(brep->rgb[2] * 255);

    //    floatVectorParams.push_back(vertices);
    //    intVectorParams.push_back(indices);
    //}
}

bool occConvertShapeToGLData(const TDF_Label& label, const Handle(AIS_InteractiveObject) occObj, const Handle(XCAFDoc_ShapeTool)& shapeTool, const Handle(XCAFDoc_ColorTool)& colorTool, std::vector<std::vector<float>>& floatVectorParams, std::vector<std::vector<int>>& intVectorParams, float mat[12], int unit)
{
    int dividend = 1;

    if (0 == unit)
    {
        dividend = 1000;
    }

    //Handle(AIS_ConnectedInteractive) tmpAisConnObject = Handle(AIS_ConnectedInteractive)::DownCast(occObj);
    //const char* typeName = occObj->DynamicType()->Name();
    
    TDF_Label refLabel = label;
    if (shapeTool->IsReference(label))
    {
        shapeTool->GetReferredShape(label, refLabel);
    }

    TopoDS_Shape occShape = shapeTool->GetShape(refLabel);

    //if (occObj.IsNull() || !occObj->IsKind(STANDARD_TYPE(AIS_Shape)))
    //{
    //    return false;
    //}

    //Handle(AIS_Shape) aisShape = Handle(AIS_Shape)::DownCast(occObj);
    //TopoDS_Shape occShape = aisShape->Shape();

    //TopLoc_Location location = shapeTool->GetLocation(label);
    //occLocToMat(location, mat);

    gp_Trsf tmpTrsf;
    occMatToTrsf(mat, tmpTrsf);

    BRepBuilderAPI_Transform xform(occShape, tmpTrsf);
    occShape = xform.Shape();

    Bnd_Box bounds;
    BRepBndLib::Add(occShape, bounds);

    bounds.SetGap(0.0);

    Standard_Real xMin, yMin, zMin, xMax, yMax, zMax;
    bounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);
    Standard_Real deflection = ((xMax - xMin) + (yMax - yMin) + (zMax - zMin)) / 300.0 * 0.2;
    Standard_Real AngDeflectionRads = 28.65 / 180.0 * M_PI;

    // Since OCCT 7.6 a value of equal 0 is not allowed any more, this can happen if a single vertex
    // should be displayed.
    if (deflection < gp::Resolution())
        deflection = Precision::Confusion();
    bool isRelative = false;        // 是否使用相对公差
    BRepMesh_IncrementalMesh(occShape, deflection, Standard_False, AngDeflectionRads, Standard_True);
    //int numFaces = 0;
    std::vector<float> vertices;
    std::vector<int> indices;
    int vertexOffset = 0;
    TopTools_IndexedMapOfShape faceMap;
    TopExp::MapShapes(occShape, TopAbs_FACE, faceMap);
    for (int i = 1; i <= faceMap.Extent(); i++) {
        TopLoc_Location aLoc;
        Handle(Poly_Triangulation) mesh = BRep_Tool::Triangulation(TopoDS::Face(faceMap(i)), aLoc);
        if (!mesh.IsNull()) {
            //mesh = triangulationOfFace(TopoDS::Face(faceMap(i)));
            //std::vector<float> vertices;
            //std::vector<int> indices;
            int nsize = mesh->NbNodes();
            for (int vi = 1; vi <= nsize; vi++)
            {
                const gp_Pnt& point = mesh->Node(vi).Transformed(aLoc.Transformation());

                vertices.push_back(point.X() / dividend);
                vertices.push_back(point.Y() / dividend);
                vertices.push_back(point.Z() / dividend);
            }

            int tsize = mesh->NbTriangles();
            for (int fi = 1; fi <= tsize; fi++)
            {
                const Poly_Triangle& triangle = mesh->Triangle(fi);
                int n1, n2, n3;
                triangle.Get(n1, n2, n3);
                indices.push_back(vertexOffset + n1 - 1);
                indices.push_back(vertexOffset + n2 - 1);
                indices.push_back(vertexOffset + n3 - 1);
            }
            vertexOffset += nsize;
        }
    }

    if ((indices.size() >= 3) && (vertices.size() >= 9))
    {
        float rgb[3] = { 0 };
        occGetShapeColor(label, colorTool, rgb);

        vertices.push_back(rgb[0] * 255);
        vertices.push_back(rgb[1] * 255);
        vertices.push_back(rgb[2] * 255);

        floatVectorParams.push_back(vertices);
        intVectorParams.push_back(indices);
    }

    return true;
}

void occTraverseShape(const Handle(XCAFDoc_ShapeTool)& shapeTool, const Handle(XCAFDoc_ColorTool)& colorTool, const TDF_Label& label, TopLoc_Location parLocation, CBrep* parBrep)
{
    //TDF_LabelSequence components;
    //if (shapeTool->GetComponents(label, components))
    //{
    //    TopLoc_Location compLocation = shapeTool->GetLocation(label);
    //    compLocation = parLocation * compLocation;

    //    CBrep* brep = new CBrep();
    //    brep->type = 0;
    //    brep->name = occGetShapeName(label);
    //    brep->parent = parBrep;
    //    occSetBrepLocation(brep, compLocation);
    //    if (NULL != parBrep)
    //    {
    //        parBrep->childern.push_back(brep);
    //    }
    //    App::ct->brepCont->addObject(brep);

    //    for (Standard_Integer compIndex = 1; compIndex <= components.Length(); ++compIndex)
    //    {
    //        TDF_Label childLabel = components.Value(compIndex);

    //        TDF_Label refLabel = childLabel;
    //        if (shapeTool->IsReference(childLabel))
    //        {
    //            shapeTool->GetReferredShape(childLabel, refLabel);
    //        }

    //        TopLoc_Location childLocation = shapeTool->GetLocation(childLabel);
    //        childLocation = parLocation * childLocation;

    //        if (shapeTool->IsAssembly(refLabel))
    //        {
    //            CBrep* childBrep = new CBrep();
    //            childBrep->type = 0;
    //            childBrep->name = occGetShapeName(refLabel);
    //            childBrep->parent = brep;
    //            occSetBrepLocation(childBrep, childLocation);
    //            if (NULL != brep)
    //            {
    //                brep->childern.push_back(childBrep);
    //            }
    //            App::ct->brepCont->addObject(childBrep);

    //            for (TDF_ChildIterator c(refLabel); c.More(); c.Next())
    //            {
    //                occTraverseShape(shapeTool, colorTool, c.Value(), childLocation, childBrep);
    //            }
    //        }
    //        else
    //        {
    //            occTraverseShape(shapeTool, colorTool, refLabel, childLocation, brep);
    //        }
    //    }
    //}
    //else
    //{
    //    TDF_Label refLabel = label;
    //    if (shapeTool->IsReference(label))
    //    {
    //        shapeTool->GetReferredShape(label, refLabel);
    //    }

    //    TopLoc_Location location = shapeTool->GetLocation(label);
    //    location = parLocation * location;

    //    if (shapeTool->IsAssembly(refLabel))
    //    {
    //        CBrep* brep = new CBrep();
    //        brep->type = 0;
    //        brep->name = occGetShapeName(refLabel);
    //        brep->parent = parBrep;
    //        occSetBrepLocation(brep, location);
    //        if (NULL != parBrep)
    //        {
    //            parBrep->childern.push_back(brep);
    //        }
    //        App::ct->brepCont->addObject(brep);

    //        for (TDF_ChildIterator c(refLabel); c.More(); c.Next())
    //        {
    //            occTraverseShape(shapeTool, colorTool, c.Value(), location, brep);
    //        }
    //    }
    //    else
    //    {
    //        TopoDS_Shape shape;
    //        shapeTool->GetShape(refLabel, shape);

    //        TopAbs_ShapeEnum type = shape.ShapeType();
    //        BRepBuilderAPI_Transform xform(shape, location.Transformation());
    //        shape = xform.Shape();

    //        std::string shapeName = occGetShapeName(refLabel);

    //        float rgb[3] = { 0 };
    //        float* pRgb = NULL;
    //        occGetShapeColor(refLabel, colorTool, rgb, pRgb);

    //        CBrep* brep = new CBrep();
    //        brep->type = 1;
    //        brep->parent = parBrep;
    //        occSetBrepLocation(brep, location);
    //        if (NULL != parBrep)
    //        {
    //            parBrep->childern.push_back(brep);
    //        }
    //        bool ret = occConvertShapeToBrep(shape, shapeName, pRgb, brep);
    //        App::ct->brepCont->addObject(brep);
    //    }
    //}
}

bool occImportEntity(const char* fileName)
{
    //Handle(XCAFApp_Application) anApp = XCAFApp_Application::GetApplication();
    //Handle(TDocStd_Document) doc;
    //anApp->NewDocument("MDTV-XCAF", doc);

    //STEPCAFControl_Reader reader;
    //reader.SetColorMode(true);
    //reader.SetNameMode(true);
    //IFSelect_ReturnStatus status = reader.ReadFile(fileName);

    //bool yes = reader.Transfer(doc);
    //if (yes)
    //{
    //    TDF_Label mainLabel = doc->Main();
    //    Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(mainLabel);
    //    Handle(XCAFDoc_ColorTool) colorTool = XCAFDoc_DocumentTool::ColorTool(mainLabel);

    //    TDF_LabelSequence tdfLabels;
    //    shapeTool->GetFreeShapes(tdfLabels);
    //    int Roots = tdfLabels.Length();

    //    for (int index = 1; index <= Roots; index++)
    //    {
    //        TDF_Label label = tdfLabels.Value(index);
    //        occTraverseShape(shapeTool, colorTool, label, TopLoc_Location(), NULL);
    //    }
    //}

    ////STEPControl_Reader reader;
    ////reader.ReadFile(fileName);

    ////reader.TransferRoots();
    ////
    ////CBrep* brep = new CBrep();
    ////occConvertShapeToBrep(reader.OneShape(), "shape", NULL, brep);
    ////App::ct->brepCont->addObject(brep);

    return true;
}

bool occConvertShapeToBrep(const TopoDS_Shape& shape, std::string shapeName, float* rgb, CBrep* brep, int unit)
{
    //if (NULL == brep)
    //{
    //    return false;
    //}

    //int dividend = 1;

    //if (0 == unit)
    //{
    //    dividend = 1000;
    //}

    //int count = 0;
    //TopExp_Explorer explorer(shape, TopAbs_FACE);
    //for (; explorer.More(); explorer.Next())
    //{
    //    std::vector<float> vertices;
    //    std::vector<int> indices;
    //    //std::vector<float> normals;

    //    TopoDS_Face face = TopoDS::Face(explorer.Current());
    //    BRepMesh_IncrementalMesh(face, 0.01);

    //    TopLoc_Location faceLocation;
    //    Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, faceLocation);
    //    if (!triangulation.IsNull()) 
    //    {
    //        int nsize = triangulation->NbNodes();
    //        for (int i = 1; i <= nsize; i++)
    //        {
    //            const gp_Pnt& point = triangulation->Node(i).Transformed(faceLocation.Transformation());
    //            vertices.push_back(point.X() / dividend);
    //            vertices.push_back(point.Y() / dividend);
    //            vertices.push_back(point.Z() / dividend);
    //        }

    //        int tsize = triangulation->NbTriangles();
    //        for (int i = 1; i <= tsize; i++) 
    //        {
    //            const Poly_Triangle& triangle = triangulation->Triangle(i);
    //            int n1, n2, n3;
    //            triangle.Get(n1, n2, n3);
    //            indices.push_back(n1 - 1);
    //            indices.push_back(n2 - 1);
    //            indices.push_back(n3 - 1);
    //        }

    //        //bool res = triangulation->HasNormals();
    //        //if (res) 
    //        //{
    //        //    for (int i = 1; i <= nsize; i++)
    //        //    {
    //        //        const gp_Dir& normal = triangulation->Normal(i);
    //        //        normals.push_back(normal.X());
    //        //        normals.push_back(normal.Y());
    //        //        normals.push_back(normal.Z());
    //        //    }
    //        //}
    //    }

    //    std::string tmpFaceName = shapeName + "_face_" + std::to_string(count++);
    //    CShape* tmpShape = (CShape*)simCreateMeshShapeExt_internal(vertices, indices, tmpFaceName, rgb);
    //    if (NULL != tmpShape)
    //    {
    //        tmpShape->setObjectHandle(App::ct->brepCont->generateBrepShapeId(brep));
    //        brep->shapes[tmpShape] = new TopoDS_Face(face);

    //        float tmpRgb[3] = { 0 };
    //        tmpShape->getColor("", 0, tmpRgb);

    //        brep->rgb[0] = tmpRgb[0];
    //        brep->rgb[1] = tmpRgb[1];
    //        brep->rgb[2] = tmpRgb[2];
    //    }

    //    int count1 = 0;
    //    TopExp_Explorer edgeExplorer;
    //    for (edgeExplorer.Init(face, TopAbs_EDGE); edgeExplorer.More(); edgeExplorer.Next())
    //    {
    //        const TopoDS_Edge& edge = TopoDS::Edge(edgeExplorer.Current());
    //        BRepAdaptor_Curve curve(edge);

    //        if (GeomAbs_Line == curve.GetType())
    //        {
    //            TopoDS_Vertex vf, vl;
    //            TopExp::Vertices(edge, vf, vl);

    //            std::vector<float> eg;

    //            gp_Pnt pvf = BRep_Tool::Pnt(vf);
    //            gp_Pnt pvl = BRep_Tool::Pnt(vl);

    //            eg.push_back(pvf.X() / dividend);
    //            eg.push_back(pvf.Y() / dividend);
    //            eg.push_back(pvf.Z() / dividend);

    //            eg.push_back(pvl.X() / dividend);
    //            eg.push_back(pvl.Y() / dividend);
    //            eg.push_back(pvl.Z() / dividend);

    //            std::string tmpEdgeName = shapeName + "_edge_" + std::to_string(count1++);
    //            C3DObject* tmpObject = (C3DObject*)simCreateEdgeExt_internal(eg, tmpEdgeName, 0, NULL);
    //            tmpObject->setObjectHandle(App::ct->brepCont->generateBrepShapeId(brep));
    //            brep->shapes[tmpObject] = new TopoDS_Edge(edge);
    //        }
    //        else if (GeomAbs_Circle == curve.GetType())
    //        {
    //            GCPnts_UniformAbscissa discretizer(curve, 0.1);

    //            if (discretizer.IsDone())
    //            {
    //                std::vector<float> eg;

    //                for (Standard_Integer i = 1; i <= discretizer.NbPoints(); ++i)
    //                {
    //                    gp_Pnt point = curve.Value(discretizer.Parameter(i));
    //                    eg.push_back(point.X() / dividend);
    //                    eg.push_back(point.Y() / dividend);
    //                    eg.push_back(point.Z() / dividend);
    //                }

    //                std::string tmpEdgeName = shapeName + "_edge_" + std::to_string(count1++);
    //                C3DObject* tmpObject = (C3DObject*)simCreateEdgeExt_internal(eg, tmpEdgeName, 1, NULL);
    //                tmpObject->setObjectHandle(App::ct->brepCont->generateBrepShapeId(brep));
    //                brep->shapes[tmpObject] = new TopoDS_Edge(edge);
    //            }
    //        }
    //        else if (GeomAbs_Ellipse == curve.GetType())
    //        {
    //            GCPnts_UniformAbscissa discretizer(curve, 0.1);

    //            if (discretizer.IsDone())
    //            {
    //                std::vector<float> eg;

    //                for (Standard_Integer i = 1; i <= discretizer.NbPoints(); ++i)
    //                {
    //                    gp_Pnt point = curve.Value(discretizer.Parameter(i));
    //                    eg.push_back(point.X() / dividend);
    //                    eg.push_back(point.Y() / dividend);
    //                    eg.push_back(point.Z() / dividend);
    //                }

    //                std::string tmpEdgeName = shapeName + "_edge_" + std::to_string(count1++);
    //                C3DObject* tmpObject = (C3DObject*)simCreateEdgeExt_internal(eg, tmpEdgeName, 2, NULL);
    //                tmpObject->setObjectHandle(App::ct->brepCont->generateBrepShapeId(brep));
    //                brep->shapes[tmpObject] = new TopoDS_Edge(edge);
    //            }
    //        }
    //        else
    //        {
    //            GCPnts_UniformAbscissa discretizer(curve, 0.1);

    //            if (discretizer.IsDone())
    //            {
    //                std::vector<float> eg;

    //                for (Standard_Integer i = 1; i <= discretizer.NbPoints(); ++i)
    //                {
    //                    gp_Pnt point = curve.Value(discretizer.Parameter(i));
    //                    eg.push_back(point.X() / dividend);
    //                    eg.push_back(point.Y() / dividend);
    //                    eg.push_back(point.Z() / dividend);
    //                }

    //                std::string tmpEdgeName = shapeName + "_edge_" + std::to_string(count1++);
    //                C3DObject* tmpObject = (C3DObject*)simCreateEdgeExt_internal(eg, tmpEdgeName, 99, NULL);
    //                tmpObject->setObjectHandle(App::ct->brepCont->generateBrepShapeId(brep));
    //                brep->shapes[tmpObject] = new TopoDS_Edge(edge);
    //            }
    //        }
    //    }
    //}

    //brep->shape = new TopoDS_Shape(shape);
    //brep->name = shapeName;

    return true;
}

int occGetFaceCentroidDummy(const TopoDS_Shape& shape, int unit)
{
    int dividend = 1;

    if (0 == unit)
    {
        dividend = 1000;
    }

    GProp_GProps props;
    BRepGProp::SurfaceProperties(shape, props);

    gp_Pnt gp = props.CentreOfMass();
    float pos[3] = { gp.X() / dividend, gp.Y() / dividend, gp.Z() / dividend };

    gp_Dir normal, utan, vtan;
    TopoDS_Face face = TopoDS::Face(shape);
    BRepLProp_SLProps localSurfaceProps(1, 1e-6);
    localSurfaceProps.SetSurface(BRepAdaptor_Surface(face));
    localSurfaceProps.SetParameters(0.5, 0.5);
    if (localSurfaceProps.IsNormalDefined()) 
    {
        normal = localSurfaceProps.Normal();
        if (face.Orientation() == TopAbs_REVERSED)
        {
            normal = gp_Dir(-normal.X(), -normal.Y(), -normal.Z());
        }
        else
        {
            normal = gp_Dir(normal.X(), normal.Y(), normal.Z());
        }

        localSurfaceProps.TangentU(utan);
        localSurfaceProps.TangentV(vtan);
    }

    float matrix[12] = { utan.X(), vtan.X(), normal.X(), pos[0], utan.Y(), vtan.Y(), normal.Y(), pos[1] , utan.Z(), vtan.Z(), normal.Z(), pos[2] };
    int objectId = simCreateDummy_internal(0.01, NULL);
    simSetObjectMatrix_internal(objectId, -1, matrix);

    return objectId;
}

int occGetCurveCentroidDummy(const TopoDS_Shape& shape, int unit)
{
    int dividend = 1;

    if (0 == unit)
    {
        dividend = 1000;
    }

    double first = 0.0;
    double last = 0.0;
    Handle(Geom_Curve) handleGeomCurve = BRep_Tool::Curve(TopoDS::Edge(shape), first, last);
    if (handleGeomCurve.IsNull()) {
        return -1;
    }
    if (handleGeomCurve->IsKind(STANDARD_TYPE(Geom_Line))) {
        std::cout << "Type is Geom_Line" << std::endl;
    }
    else if (handleGeomCurve->IsKind(STANDARD_TYPE(Geom_Circle))) {
        std::cout << "Type is Geom_Circle" << std::endl;
    }
    else if (handleGeomCurve->IsKind(STANDARD_TYPE(Geom_TrimmedCurve))) {
        std::cout << "Type is Geom_Circle" << std::endl;
    }
    else if (handleGeomCurve->IsKind(STANDARD_TYPE(Geom_BoundedCurve))) {
        std::cout << "Type is Geom_Circle" << std::endl;
    }

    BRepAdaptor_Curve curveAdap(TopoDS::Edge(shape));
    BRepLProp_CLProps curveProp(curveAdap, 1, 0.00001);

    gp_Dir normal, utan, vtan;
    //curveProp.Normal(normal);
    //curveProp.Tangent(utan);

    //vtan = normal.Crossed(utan);

    gp_Pnt gp;
    curveProp.CentreOfCurvature(gp);
    
    float pos[3] = { gp.X() / dividend, gp.Y() / dividend, gp.Z() / dividend };
    float matrix[12] = { utan.X(), vtan.X(), normal.X(), pos[0], utan.Y(), vtan.Y(), normal.Y(), pos[1] , utan.Z(), vtan.Z(), normal.Z(), pos[2] };

    int objectId = simCreateDummy_internal(0.01, NULL);
    simSetObjectMatrix_internal(objectId, -1, matrix);

    return objectId;
}

bool occGetEntityMesh(int entityId, std::vector<std::vector<float>>& floatVectorParams, std::vector<std::vector<int>>& intVectorParams, float mat[12])
{
    //CBrep* brep = App::ct->brepCont->getObject(entityId);
    //if (NULL == brep)
    //{
    //    return false;
    //}

    //C7Vector tmpC7vec(C4Vector(brep->location[3], brep->location[4], brep->location[5], brep->location[6]), C3Vector(brep->location[0], brep->location[1], brep->location[2]));
    //tmpC7vec.getMatrix().copyToInterface(mat);

    //std::vector<CBrep*> breps;
    //occTraverseBrepNode(brep, breps);

    //if (0 == breps.size())
    //{
    //    return false;
    //}

    //for (int i = 0; i < breps.size(); i++)
    //{
    //    occConvertShapeToGLData(breps.at(i), floatVectorParams, intVectorParams);
    //}

    return true;
}

bool occCreateCylinder(TopoDS_Shape& shape, float pos[3], float dir[3], double radius, double height, std::string name)
{
    gp_Ax2 position(gp_Pnt(pos[0], pos[1], pos[2]), gp_Dir(dir[0], dir[1], dir[2]));
    shape = BRepPrimAPI_MakeCylinder(position, radius, height).Shape();

    //CBrep* brep = new CBrep();
    //occConvertShapeToBrep(aCylinder, name, NULL, brep);
    //App::ct->brepCont->addObject(brep);

    return true;
}

bool occCreateBox(TopoDS_Shape& shape, float pos[3], float dir[3], float length, float width, float height)
{
    gp_Ax2 position(gp_Pnt(pos[0], pos[1], pos[2]), gp_Dir(dir[0], dir[1], dir[2]));
    shape = BRepPrimAPI_MakeBox(position, length, width, height);

    return true;
}

bool occCreateSphere(TopoDS_Shape& shape, float pos[3], float radius)
{
    shape = BRepPrimAPI_MakeSphere(gp_Pnt(pos[0], pos[1], pos[2]), radius);

    return true;
}

bool occSetBrepRigidBody(int brepId, int rigidBodyId)
{
    //CBrep* brep = App::ct->brepCont->getObject(brepId);
    //if (NULL == brep)
    //{
    //    return false;
    //}

    //if (-1 == rigidBodyId)
    //{
    //    brep->rigidBodyName = "";

    //    std::vector<CBrep*> breps;
    //    occTraverseBrepNode(brep, breps);

    //    if (0 == breps.size())
    //    {
    //        return false;
    //    }

    //    for (int i = 0; i < breps.size(); i++)
    //    {
    //        std::map<C3DObject*, TopoDS_Shape*>::iterator it = breps.at(i)->shapes.begin();
    //        for (; it != breps.at(i)->shapes.end(); it++)
    //        {
    //            App::ct->objCont->makeObjectChildOf(it->first, nullptr);
    //        }
    //    }
    //}
    //else
    //{
    //    C3DObject* parObject = App::ct->objCont->getObjectFromHandle(rigidBodyId);
    //    if (NULL == parObject)
    //    {
    //        return false;
    //    }

    //    brep->rigidBodyName = parObject->getObjectName();

    //    std::vector<CBrep*> breps;
    //    occTraverseBrepNode(brep, breps);

    //    if (0 == breps.size())
    //    {
    //        return false;
    //    }

    //    for (int i = 0; i < breps.size(); i++)
    //    {
    //        std::map<C3DObject*, TopoDS_Shape*>::iterator it = breps.at(i)->shapes.begin();
    //        for (; it != breps.at(i)->shapes.end(); it++)
    //        {
    //            App::ct->objCont->makeObjectChildOf(it->first, parObject);
    //        }
    //    }
    //}

    return true;
}

bool occSetShapeLocation(TopoDS_Shape& shape, float c7vec[7], int unit)
{
    int multiplier = 1;

    if (0 == unit)
    {
        multiplier = 1000;
    }

    gp_Trsf trans;
    trans.SetRotation(gp_Quaternion(c7vec[3], c7vec[4], c7vec[5], c7vec[6]));
    trans.SetTranslation(gp_Vec(c7vec[0] * multiplier, c7vec[1] * multiplier, c7vec[2] * multiplier));

    BRepBuilderAPI_Transform xform(shape, trans);
    shape = xform.Shape();

    return true;
}

void occSetBrepLocation(CBrep* brep, TopLoc_Location& location, int unit)
{
    //if (NULL == brep)
    //{
    //    return;
    //}

    //int dividend = 1;

    //if (0 == unit)
    //{
    //    dividend = 1000;
    //}

    //gp_Trsf tmpTrsf = location.Transformation();
    //gp_Quaternion tmpQuat = location.Transformation().GetRotation();
    //brep->location[0] = tmpTrsf.Value(1, 4) / dividend;
    //brep->location[1] = tmpTrsf.Value(2, 4) / dividend;
    //brep->location[2] = tmpTrsf.Value(3, 4) / dividend;
    //brep->location[3] = tmpQuat.X();
    //brep->location[4] = tmpQuat.Y();
    //brep->location[5] = tmpQuat.Z();
    //brep->location[6] = tmpQuat.W();
}

void occMatToTrsf(float mat[12], gp_Trsf& trsf, int unit)
{
    float multiplier = 1;
    if (0 == unit)
    {
        multiplier = 1000;
    }

    double r11 = mat[0], r12 = mat[1], r13 = mat[2], tx = mat[3] * multiplier; // 第一行
    double r21 = mat[4], r22 = mat[5], r23 = mat[6], ty = mat[7] * multiplier; // 第二行
    double r31 = mat[8], r32 = mat[9], r33 = mat[10], tz = mat[11] * multiplier; // 第三行

    trsf.SetValues(
        r11, r12, r13, tx, // 第一行
        r21, r22, r23, ty, // 第二行
        r31, r32, r33, tz  // 第三行
    );
}

void occDefaultTrsf(gp_Trsf& trsf)
{
    double r11 = 1, r12 = 0, r13 = 0, tx = 0; // 第一行
    double r21 = 0, r22 = 1, r23 = 0, ty = 0; // 第二行
    double r31 = 0, r32 = 0, r33 = 1, tz = 0; // 第三行
    trsf.SetValues(
        r11, r12, r13, tx, // 第一行
        r21, r22, r23, ty, // 第二行
        r31, r32, r33, tz  // 第三行
    );
}
#endif // 0


void occLocToMat(TopLoc_Location& loc, float mat[12], int unit)
{
    float dividend = 1;
    if (0 == unit)
    {
        dividend = 1000;
    }

    gp_Trsf trsf = loc.Transformation();

    mat[0] = trsf.Value(1, 1), mat[1] = trsf.Value(1, 2), mat[2] = trsf.Value(1, 3), mat[3] = trsf.Value(1, 4) / dividend; // 第一行
    mat[4] = trsf.Value(2, 1), mat[5] = trsf.Value(2, 2), mat[6] = trsf.Value(2, 3), mat[7] = trsf.Value(2, 4) / dividend; // 第二行
    mat[8] = trsf.Value(3, 1), mat[9] = trsf.Value(3, 2), mat[10] = trsf.Value(3, 3), mat[11] = trsf.Value(3, 4) / dividend; // 第三行
}

void occMatToLoc(float mat[12], TopLoc_Location& loc, int unit)
{
    float multiplier = 1;
    if (0 == unit)
    {
        multiplier = 1000;
    }

    double r11 = mat[0], r12 = mat[1], r13 = mat[2], tx = mat[3] * multiplier; // 第一行
    double r21 = mat[4], r22 = mat[5], r23 = mat[6], ty = mat[7] * multiplier; // 第二行
    double r31 = mat[8], r32 = mat[9], r33 = mat[10], tz = mat[11] * multiplier; // 第三行

    gp_Trsf trsf;

    trsf.SetValues(
        r11, r12, r13, tx, // 第一行
        r21, r22, r23, ty, // 第二行
        r31, r32, r33, tz  // 第三行
    );

    TopLoc_Location tmpLoc(trsf);
    loc = tmpLoc;
}

void occTrsfToMat(gp_Trsf& trsf, float mat[12], int unit)
{
    float dividend = 1;
    if (0 == unit)
    {
        dividend = 1000;
    }

    mat[0] = trsf.Value(1, 1), mat[1] = trsf.Value(1, 2), mat[2] = trsf.Value(1, 3), mat[3] = trsf.Value(1, 4) / dividend; // 第一行
    mat[4] = trsf.Value(2, 1), mat[5] = trsf.Value(2, 2), mat[6] = trsf.Value(2, 3), mat[7] = trsf.Value(2, 4) / dividend; // 第二行
    mat[8] = trsf.Value(3, 1), mat[9] = trsf.Value(3, 2), mat[10] = trsf.Value(3, 3), mat[11] = trsf.Value(3, 4) / dividend; // 第三行
}

void occAx2ToMat(gp_Ax2& ax2, float mat[12], int unit)
{
    float dividend = 1;
    if (0 == unit)
    {
        dividend = 1000;
    }

    mat[3] = ax2.Location().X() / dividend;
    mat[7] = ax2.Location().Y() / dividend;
    mat[11] = ax2.Location().Z() / dividend;

    gp_Dir xDir = ax2.XDirection();
    gp_Dir yDir = ax2.YDirection();

    mat[0] = xDir.X();
    mat[4] = xDir.Y();
    mat[8] = xDir.Z();

    mat[1] = yDir.X();
    mat[5] = yDir.Y();
    mat[9] = yDir.Z();

    gp_Dir zDir = xDir.Crossed(yDir);
    
    mat[2] = zDir.X();
    mat[6] = zDir.Y();
    mat[10] = zDir.Z();
}

void occMatToAx2(float mat[12], gp_Ax2& ax2, int unit)
{
    float multiplier = 1;
    if (0 == unit)
    {
        multiplier = 1000;
    }

    gp_Ax2 tmpAx2(gp_Pnt(mat[3] * multiplier, mat[7] * multiplier, mat[11] * multiplier), gp_Dir(mat[2], mat[6], mat[10]), gp_Dir(mat[0], mat[4], mat[8]));
    ax2 = tmpAx2;
}

#if 0
bool occGetFaceCentroidCsys(const TopoDS_Shape& shape, Handle(AIS_Trihedron)& csysTrihedron, float mat[12], gp_Pnt& centroid, int normalIndex, int unit)
{
    int dividend = 1;

    if (0 == unit)
    {
        dividend = 1000;
    }

    GProp_GProps props;
    BRepGProp::SurfaceProperties(shape, props);

    centroid = props.CentreOfMass();
    float pos[3] = { centroid.X() / dividend, centroid.Y() / dividend, centroid.Z() / dividend };

    gp_Dir normal, utan, vtan;
    TopoDS_Face face = TopoDS::Face(shape);
    BRepLProp_SLProps localSurfaceProps(1, 1e-6);
    localSurfaceProps.SetSurface(BRepAdaptor_Surface(face));

    localSurfaceProps.SetParameters(0.5, 0.5);

    if (localSurfaceProps.IsNormalDefined())
    {
        normal = localSurfaceProps.Normal();
        if (face.Orientation() == TopAbs_REVERSED)
        {
            normal = gp_Dir(-normal.X(), -normal.Y(), -normal.Z());
        }
        else
        {
            normal = gp_Dir(normal.X(), normal.Y(), normal.Z());
        }

        localSurfaceProps.TangentU(utan);
        localSurfaceProps.TangentV(vtan);
    }

    gp_Pnt oPnt(pos[0], pos[1], pos[2]);
    gp_Dir aixX;
    gp_Dir aixY;
    gp_Dir aixZ;
    gp_Ax2 ax2;
    //if (1 == normalIndex)
    //{
    //    aixX = normal;
    //    aixY = utan;
    //    ax2 = gp_Ax2(oPnt, aixX, aixY);
    //}
    //else if (2 == normalIndex)
    //{
    //    aixY = normal;
    //    aixZ = utan;
    //    ax2 = gp_Ax2(oPnt, aixY, aixZ);
    //}
    //else if ((3 == normalIndex) || (4 == normalIndex))
    //{
    //    aixZ = normal;
    //    aixX = utan;
    //    ax2 = gp_Ax2(oPnt, aixZ, aixX);
    //}
    aixZ = normal;
    aixX = utan;
    ax2 = gp_Ax2(oPnt, aixZ, aixX);

    occAx2ToMat(ax2, mat);

    float mat1[12] = { 0 };
    gp_Trsf trsf1;
    occDefaultTrsf(trsf1);
    occTrsfToMat(trsf1, mat1);
    gp_Ax2 ax21;
    occMatToAx2(mat1, ax21);

    Handle(Geom_Axis2Placement) trihedronAxis = new Geom_Axis2Placement(ax21);
    csysTrihedron = new AIS_Trihedron(trihedronAxis);
    occSetTrihedronDefaultStyle(csysTrihedron);

    //delete trihedronAxis.get();

    return true;
}

bool occGetFaceUVParamCsys(const TopoDS_Shape& shape, Handle(AIS_Trihedron)& csysTrihedron, float mat[12], double u, double v, gp_Pnt pos, int unit)
{
    int dividend = 1;

    if (0 == unit)
    {
        dividend = 1000;
    }

    GProp_GProps props;
    BRepGProp::SurfaceProperties(shape, props);

    gp_Dir normal, utan, vtan;
    TopoDS_Face face = TopoDS::Face(shape);
    BRepLProp_SLProps localSurfaceProps(1, 1e-6);
    localSurfaceProps.SetSurface(BRepAdaptor_Surface(face));

    localSurfaceProps.SetParameters(u, v);

    if (localSurfaceProps.IsNormalDefined())
    {
        normal = localSurfaceProps.Normal();
        if (face.Orientation() == TopAbs_REVERSED)
        {
            normal = gp_Dir(-normal.X(), -normal.Y(), -normal.Z());
        }
        else
        {
            normal = gp_Dir(normal.X(), normal.Y(), normal.Z());
        }

        localSurfaceProps.TangentU(utan);
        localSurfaceProps.TangentV(vtan);
    }

    gp_Pnt oPnt(pos);
    gp_Dir aixX(utan.X(), utan.Y(), utan.Z());
    gp_Dir aixZ(normal.X(), normal.Y(), normal.Z());
    gp_Ax2 ax2 = gp_Ax2(oPnt, aixZ, aixX);

    occAx2ToMat(ax2, mat);

    float mat1[12] = { 0 };
    gp_Trsf trsf1;
    occDefaultTrsf(trsf1);
    occTrsfToMat(trsf1, mat1);
    gp_Ax2 ax21;
    occMatToAx2(mat1, ax21);

    Handle(Geom_Axis2Placement) trihedronAxis = new Geom_Axis2Placement(ax21);
    csysTrihedron = new AIS_Trihedron(trihedronAxis);
    occSetTrihedronDefaultStyle(csysTrihedron);

    //delete trihedronAxis.get();

    return true;
}

bool ArePointsCollinear(const gp_Pnt& p1, const gp_Pnt& p2, const gp_Pnt& p3, double tolerance = 1e-6) {
    gp_Vec v1(p1, p2); // 向量 p1 -> p2
    gp_Vec v2(p1, p3); // 向量 p1 -> p3

    // 计算叉积
    gp_Vec crossProduct = v1.Crossed(v2);

    // 检查叉积的模是否接近零
    return (crossProduct.Magnitude() < tolerance);
}

bool occGetCenterofCurvatureCsys(const TopoDS_Shape& shape, Handle(AIS_Trihedron)& csysTrihedron, float mat[12], gp_Pnt &centerofCurvature)
{
    // 检查输入形状是否为边
    if (shape.ShapeType() != TopAbs_EDGE)
    {
        throw std::runtime_error("Input shape is not an edge.");
        return false;
    }

    // 将 TopoDS_Shape 转换为 TopoDS_Edge
    TopoDS_Edge edge = TopoDS::Edge(shape);

    // 从边中提取几何曲线
    Standard_Real firstParam, lastParam;
    Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, firstParam, lastParam);
    if (curve.IsNull())
    {
        throw std::runtime_error("Failed to extract curve from edge.");
        return false;
    }

    //BRepAdaptor_Curve curveAdaptor(edge);

    //GeomAbs_CurveType type = curveAdaptor.GetType();
    //if (curveAdaptor.GetType() == GeomAbs_Circle)
    //{
    //    centerofCurvature = curveAdaptor.Circle().Location();
    //}

    // 初始化曲线属性计算器
    GeomLProp_CLProps props(curve, 2, Precision::Confusion());

    // 设置参数
    props.SetParameter(firstParam);
    gp_Pnt p1 = props.Value();
    
    props.SetParameter(lastParam);
    gp_Pnt p3 = props.Value();

    props.SetParameter((firstParam + lastParam) / 2);
    gp_Pnt p2 = props.Value();
    

    // 获取曲率
    double curvature = props.Curvature();
    if (curvature < Precision::Confusion())
    {
        std::cout << "曲率太小，无法计算曲率中心。" << std::endl;
        return false;
    }

    props.CentreOfCurvature(centerofCurvature);

    if (!ArePointsCollinear(p1, p2, p3)) //三点不共线
    {
        // 使用三个点创建圆
        GC_MakeCircle makeCircle(p1, p2, p3);

        // 检查是否创建成功
        if (makeCircle.IsDone()) {
            // 获取圆的几何对象
            Handle(Geom_Circle) circle = makeCircle.Value();

            // 输出圆的属性
            centerofCurvature = circle.get()->Location();
        }
    }

    // 获取切线
    gp_Dir utan;
    props.Tangent(utan);

    // 获取法线
    gp_Dir normal;
    props.Normal(normal);

    gp_Pnt oPnt(centerofCurvature.X(), centerofCurvature.Y(), centerofCurvature.Z());
    gp_Dir aixX(utan.X(), utan.Y(), utan.Z());
    gp_Dir aixZ(normal.X(), normal.Y(), normal.Z());
    gp_Ax2 ax2 = gp_Ax2(oPnt, aixZ, aixX);

    occAx2ToMat(ax2, mat);
    return true;
}

bool occFindLabelByShapeTag(const TDF_Label& rootLabel, const Standard_Integer shapeTag, TDF_Label& shapeLabel)
{
    shapeLabel = rootLabel.FindChild(shapeTag, true);
    if (shapeLabel.IsNull())
    {
        return false;
    }

    return true;
}

bool occGetGfxObjectShape(Handle(AIS_InteractiveObject)& aisObj, TopoDS_Shape& shape)
{
    if (aisObj->DynamicType() == STANDARD_TYPE(AIS_ConnectedInteractive))
    {
        AIS_ConnectedInteractive* tmpAisConnObj = (AIS_ConnectedInteractive*)aisObj.get();
        if (NULL != tmpAisConnObj)
        {
            Handle(AIS_InteractiveObject) tmpGfxObject = tmpAisConnObj->ConnectedTo();
            if (tmpGfxObject->DynamicType() == STANDARD_TYPE(XCAFPrs_AISObject))
            {
                XCAFPrs_AISObject* tmpXcafAisObj = (XCAFPrs_AISObject*)tmpGfxObject.get();
                if (NULL != tmpXcafAisObj)
                {
                    shape = tmpXcafAisObj->Shape();
                }
            }
            else if (tmpGfxObject->DynamicType() == STANDARD_TYPE(AIS_Shape))
            {
                AIS_Shape* tmpAisShapeObj = (AIS_Shape*)tmpGfxObject.get();
                if (NULL != tmpAisShapeObj)
                {
                    shape = tmpAisShapeObj->Shape();
                }
            }
            else if (tmpGfxObject->DynamicType() == STANDARD_TYPE(AIS_ColoredShape))
            {
                AIS_ColoredShape* tmpAisColorShapeObj = (AIS_ColoredShape*)tmpGfxObject.get();
                if (NULL != tmpAisColorShapeObj)
                {
                    shape = tmpAisColorShapeObj->Shape();
                }
            }
            else
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }
    else if (aisObj->DynamicType() == STANDARD_TYPE(AIS_Shape))
    {
        AIS_Shape* tmpAisShapeObj = (AIS_Shape*)aisObj.get();
        if (NULL != tmpAisShapeObj)
        {
            shape = tmpAisShapeObj->Shape();
        }
        else
        {
            return false;
        }
    }
    else if (aisObj->DynamicType() == STANDARD_TYPE(XCAFPrs_AISObject))
    {
        XCAFPrs_AISObject* tmpXcafAisObj = (XCAFPrs_AISObject*)aisObj.get();
        if (NULL != tmpXcafAisObj)
        {
            shape = tmpXcafAisObj->Shape();
        }
        else
        {
            return false;
        }
    }
    else if (aisObj->DynamicType() == STANDARD_TYPE(AIS_ColoredShape))
    {
        AIS_ColoredShape* tmpAisColorShapeObj = (AIS_ColoredShape*)aisObj.get();
        if (NULL != tmpAisColorShapeObj)
        {
            shape = tmpAisColorShapeObj->Shape();
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }

    return true;
}

void occSetTrihedronDefaultStyle(Handle(AIS_Trihedron)& aisTrihedron)
{
    aisTrihedron->SetDatumDisplayMode(Prs3d_DM_Shaded);
    aisTrihedron->SetDrawArrows(true);
    aisTrihedron->Attributes()->DatumAspect()->LineAspect(Prs3d_DP_XArrow)->SetWidth(2);
    aisTrihedron->Attributes()->DatumAspect()->LineAspect(Prs3d_DP_YArrow)->SetWidth(2);
    aisTrihedron->Attributes()->DatumAspect()->LineAspect(Prs3d_DP_ZArrow)->SetWidth(2);
    aisTrihedron->Attributes()->DatumAspect()->LineAspect(Prs3d_DP_XAxis)->SetWidth(1.5);
    aisTrihedron->Attributes()->DatumAspect()->LineAspect(Prs3d_DP_YAxis)->SetWidth(1.5);
    aisTrihedron->Attributes()->DatumAspect()->LineAspect(Prs3d_DP_ZAxis)->SetWidth(1.5);
    aisTrihedron->SetDatumPartColor(Prs3d_DP_XArrow, Quantity_NOC_RED2);
    aisTrihedron->SetDatumPartColor(Prs3d_DP_YArrow, Quantity_NOC_GREEN2);
    aisTrihedron->SetDatumPartColor(Prs3d_DP_ZArrow, Quantity_NOC_BLUE2);
    aisTrihedron->SetDatumPartColor(Prs3d_DP_XAxis, Quantity_NOC_RED2);
    aisTrihedron->SetDatumPartColor(Prs3d_DP_YAxis, Quantity_NOC_GREEN2);
    aisTrihedron->SetDatumPartColor(Prs3d_DP_ZAxis, Quantity_NOC_BLUE2);
    aisTrihedron->SetLabel(Prs3d_DP_XAxis, "X");
    aisTrihedron->SetLabel(Prs3d_DP_YAxis, "Y");
    aisTrihedron->SetLabel(Prs3d_DP_ZAxis, "Z");
    aisTrihedron->SetTextColor(Prs3d_DP_XAxis, Quantity_NOC_RED2);
    aisTrihedron->SetTextColor(Prs3d_DP_YAxis, Quantity_NOC_GREEN2);
    aisTrihedron->SetTextColor(Prs3d_DP_ZAxis, Quantity_NOC_BLUE2);
    aisTrihedron->SetSize(100);

    //aisTrihedron->SetTransformPersistence(new Graphic3d_TransformPers(Graphic3d_TMF_ZoomPers, anOrg));
    //aisTrihedron->Attributes()->SetZLayer(Graphic3d_ZLayerId_Topmost);
    aisTrihedron->SetInfiniteState(true);
}

bool occCreateTrihedron(Handle(AIS_Trihedron)& csysTrihedron, float mat[12], int unit)
{
    float mat1[12] = { 0 };
    gp_Trsf trsf1;
    occDefaultTrsf(trsf1);
    occTrsfToMat(trsf1, mat1);
    gp_Ax2 ax21;
    occMatToAx2(mat1, ax21);

    Handle(Geom_Axis2Placement) trihedronAxis = new Geom_Axis2Placement(ax21);
    csysTrihedron = new AIS_Trihedron(trihedronAxis);
    occSetTrihedronDefaultStyle(csysTrihedron);

    gp_Trsf trsf;
    occMatToTrsf(mat, trsf);
    csysTrihedron->SetLocalTransformation(trsf);

    return true;
}

bool occCreatePointShape(Handle(AIS_Shape)& pointShape, float mat[12], int unit)
{
    gp_Pnt tmpVertexes[9] = { gp_Pnt(0,0,0),
        gp_Pnt(-0.5, -0.5, -0.5), gp_Pnt(0.5, -0.5, -0.5), gp_Pnt(0.5, 0.5, -0.5),
                                    gp_Pnt(-0.5, 0.5, -0.5), gp_Pnt(-0.5, -0.5, 0.5), gp_Pnt(0.5, -0.5, 0.5),
                                    gp_Pnt(0.5, 0.5, 0.5), gp_Pnt(-0.5, 0.5, 0.5) };

    int ind[36] = {
        1, 2, 3, 1, 3, 4,
        5, 6, 7, 5, 7, 8,
        1, 5, 8, 1, 8, 4,
        2, 6, 7, 2, 7, 3,
        4, 3, 7, 4, 7, 8,
        1, 2, 6, 1, 6, 5
    };

    TColgp_Array1OfPnt nodes(1, 36);
    Poly_Array1OfTriangle triangles(1, 12);
    std::vector<Quantity_Color> vertexColors;
    Quantity_Color tmpColor(1, 1, 1, Mayo::TKernelUtils::preferredRgbColorType());

    int tmpNodeIndex = 1;
    int tmpTriangleIndex = 1;

    for (int i = 0; i < 12; i++, tmpTriangleIndex++)
    {
        nodes(tmpNodeIndex).SetCoord(tmpVertexes[ind[3 * i + 0]].X(), tmpVertexes[ind[3 * i + 0]].Y(), tmpVertexes[ind[3 * i + 0]].Z());
        vertexColors.push_back(tmpColor);
        triangles(tmpTriangleIndex).Set(1, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes[ind[3 * i + 1]].X(), tmpVertexes[ind[3 * i + 1]].Y(), tmpVertexes[ind[3 * i + 1]].Z());
        vertexColors.push_back(tmpColor);
        triangles(tmpTriangleIndex).Set(2, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes[ind[3 * i + 2]].X(), tmpVertexes[ind[3 * i + 2]].Y(), tmpVertexes[ind[3 * i + 2]].Z());
        vertexColors.push_back(tmpColor);
        triangles(tmpTriangleIndex).Set(3, tmpNodeIndex++);
    }

    Handle(Poly_Triangulation) tmpMesh = new Poly_Triangulation(nodes, triangles);
    TopoDS_Shape* tmpOccShape = new TopoDS_Shape(Mayo::BRepUtils::makeFace(tmpMesh));

    gp_Trsf tmpTrsf;
    occMatToTrsf(mat, tmpTrsf);

    BRepBuilderAPI_Transform xform(*tmpOccShape, tmpTrsf);
    *tmpOccShape = xform.Shape();

    pointShape = new AIS_Shape(*tmpOccShape);

    delete tmpOccShape;
    //delete tmpMesh.get();

    return true;
}

bool occCreateRevoluteJointShape(TopoDS_Shape& shape, std::vector<Quantity_Color>& vertexColors, float mat[12], int unit)
{
    double r = 4, r1 = 8, a = 0, b = 0, x, y;

    gp_Pnt tmpVertexes[13], tmpVertexes1[13], tmpVertexes2[13], tmpVertexes3[13];
    tmpVertexes[12] = gp_Pnt(0, 0, -85);
    tmpVertexes1[12] = gp_Pnt(0, 0, 85);
    tmpVertexes2[12] = gp_Pnt(0, 0, -70);
    tmpVertexes3[12] = gp_Pnt(0, 0, 70);

    for (int i = 0, k = 0; ((i < 360) && (k < 12)); i += 30, k++)
    {
        x = a + r * cos(i * 3.141592 / 180);
        y = b + r * sin(i * 3.141592 / 180);

        tmpVertexes[k] = gp_Pnt(x, y, -85);
        tmpVertexes1[k] = gp_Pnt(x, y, 85);

        x = a + r1 * cos(i * 3.141592 / 180);
        y = b + r1 * sin(i * 3.141592 / 180);

        tmpVertexes2[k] = gp_Pnt(x, y, -70);
        tmpVertexes3[k] = gp_Pnt(x, y, 70);
    }

    int ind[12 * 3] = { 0 };

    for (int i = 0; i < 12; i++)
    {
        if (11 == i)
        {
            ind[i * 3 + 0] = 12;
            ind[i * 3 + 1] = i;
            ind[i * 3 + 2] = 0;
        }
        else
        {
            ind[i * 3 + 0] = 12;
            ind[i * 3 + 1] = i;
            ind[i * 3 + 2] = i + 1;
        }
    }

    int ind1[24 * 3] = { 0 };

    for (int i = 0; i < 12; i++)
    {
        if (11 == i)
        {
            ind1[i * 6 + 0] = i;
            ind1[i * 6 + 1] = i;
            ind1[i * 6 + 2] = 0;
            ind1[i * 6 + 3] = i;
            ind1[i * 6 + 4] = 0;
            ind1[i * 6 + 5] = 0;
        }
        else
        {
            ind1[i * 6 + 0] = i;
            ind1[i * 6 + 1] = i;
            ind1[i * 6 + 2] = i + 1;
            ind1[i * 6 + 3] = i;
            ind1[i * 6 + 4] = i + 1;
            ind1[i * 6 + 5] = i + 1;
        }
    }

    TColgp_Array1OfPnt nodes(1, (48 * 3) * 2);
    Poly_Array1OfTriangle triangles(1, 48 * 2);
    Quantity_Color tmpColor(1, 0, 0, Mayo::TKernelUtils::preferredRgbColorType());
    Quantity_Color tmpColor1(0, 0, 1, Mayo::TKernelUtils::preferredRgbColorType());

    int tmpNodeIndex = 1;
    int tmpTriangleIndex = 1;

    for (int i = 0; i < 12; i++, tmpTriangleIndex++)
    {
        nodes(tmpNodeIndex).SetCoord(tmpVertexes[ind[3 * i + 0]].X(), tmpVertexes[ind[3 * i + 0]].Y(), tmpVertexes[ind[3 * i + 0]].Z());
        vertexColors.push_back(tmpColor1);
        triangles(tmpTriangleIndex).Set(1, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes[ind[3 * i + 1]].X(), tmpVertexes[ind[3 * i + 1]].Y(), tmpVertexes[ind[3 * i + 1]].Z());
        vertexColors.push_back(tmpColor1);
        triangles(tmpTriangleIndex).Set(2, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes[ind[3 * i + 2]].X(), tmpVertexes[ind[3 * i + 2]].Y(), tmpVertexes[ind[3 * i + 2]].Z());
        vertexColors.push_back(tmpColor1);
        triangles(tmpTriangleIndex).Set(3, tmpNodeIndex++);
    }

    for (int i = 0; i < 12; i++, tmpTriangleIndex++)
    {
        nodes(tmpNodeIndex).SetCoord(tmpVertexes1[ind[3 * i + 0]].X(), tmpVertexes1[ind[3 * i + 0]].Y(), tmpVertexes1[ind[3 * i + 0]].Z());
        vertexColors.push_back(tmpColor1);
        triangles(tmpTriangleIndex).Set(1, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes1[ind[3 * i + 1]].X(), tmpVertexes1[ind[3 * i + 1]].Y(), tmpVertexes1[ind[3 * i + 1]].Z());
        vertexColors.push_back(tmpColor1);
        triangles(tmpTriangleIndex).Set(2, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes1[ind[3 * i + 2]].X(), tmpVertexes1[ind[3 * i + 2]].Y(), tmpVertexes1[ind[3 * i + 2]].Z());
        vertexColors.push_back(tmpColor1);
        triangles(tmpTriangleIndex).Set(3, tmpNodeIndex++);
    }

    for (int i = 0; i < 12; i++, tmpTriangleIndex++)
    {
        nodes(tmpNodeIndex).SetCoord(tmpVertexes2[ind[3 * i + 0]].X(), tmpVertexes2[ind[3 * i + 0]].Y(), tmpVertexes2[ind[3 * i + 0]].Z());
        vertexColors.push_back(tmpColor);
        triangles(tmpTriangleIndex).Set(1, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes2[ind[3 * i + 1]].X(), tmpVertexes2[ind[3 * i + 1]].Y(), tmpVertexes2[ind[3 * i + 1]].Z());
        vertexColors.push_back(tmpColor);
        triangles(tmpTriangleIndex).Set(2, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes2[ind[3 * i + 2]].X(), tmpVertexes2[ind[3 * i + 2]].Y(), tmpVertexes2[ind[3 * i + 2]].Z());
        vertexColors.push_back(tmpColor);
        triangles(tmpTriangleIndex).Set(3, tmpNodeIndex++);
    }

    for (int i = 0; i < 12; i++, tmpTriangleIndex++)
    {
        nodes(tmpNodeIndex).SetCoord(tmpVertexes3[ind[3 * i + 0]].X(), tmpVertexes3[ind[3 * i + 0]].Y(), tmpVertexes3[ind[3 * i + 0]].Z());
        vertexColors.push_back(tmpColor);
        triangles(tmpTriangleIndex).Set(1, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes3[ind[3 * i + 1]].X(), tmpVertexes3[ind[3 * i + 1]].Y(), tmpVertexes3[ind[3 * i + 1]].Z());
        vertexColors.push_back(tmpColor);
        triangles(tmpTriangleIndex).Set(2, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes3[ind[3 * i + 2]].X(), tmpVertexes3[ind[3 * i + 2]].Y(), tmpVertexes3[ind[3 * i + 2]].Z());
        vertexColors.push_back(tmpColor);
        triangles(tmpTriangleIndex).Set(3, tmpNodeIndex++);
    }

    for (int i = 0; i < 12; i++, tmpTriangleIndex++)
    {
        nodes(tmpNodeIndex).SetCoord(tmpVertexes1[ind1[6 * i + 0]].X(), tmpVertexes1[ind1[6 * i + 0]].Y(), tmpVertexes1[ind1[6 * i + 0]].Z());
        vertexColors.push_back(tmpColor1);
        triangles(tmpTriangleIndex).Set(1, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes[ind1[6 * i + 1]].X(), tmpVertexes[ind1[6 * i + 1]].Y(), tmpVertexes[ind1[6 * i + 1]].Z());
        vertexColors.push_back(tmpColor1);
        triangles(tmpTriangleIndex).Set(2, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes[ind1[6 * i + 2]].X(), tmpVertexes[ind1[6 * i + 2]].Y(), tmpVertexes[ind1[6 * i + 2]].Z());
        vertexColors.push_back(tmpColor1);
        triangles(tmpTriangleIndex).Set(3, tmpNodeIndex++);

        tmpTriangleIndex++;

        nodes(tmpNodeIndex).SetCoord(tmpVertexes1[ind1[6 * i + 3]].X(), tmpVertexes1[ind1[6 * i + 3]].Y(), tmpVertexes1[ind1[6 * i + 3]].Z());
        vertexColors.push_back(tmpColor1);
        triangles(tmpTriangleIndex).Set(1, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes[ind1[6 * i + 4]].X(), tmpVertexes[ind1[6 * i + 4]].Y(), tmpVertexes[ind1[6 * i + 4]].Z());
        vertexColors.push_back(tmpColor1);
        triangles(tmpTriangleIndex).Set(2, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes1[ind1[6 * i + 5]].X(), tmpVertexes1[ind1[6 * i + 5]].Y(), tmpVertexes1[ind1[6 * i + 5]].Z());
        vertexColors.push_back(tmpColor1);
        triangles(tmpTriangleIndex).Set(3, tmpNodeIndex++);
    }

    for (int i = 0; i < 12; i++, tmpTriangleIndex++)
    {
        nodes(tmpNodeIndex).SetCoord(tmpVertexes3[ind1[6 * i + 0]].X(), tmpVertexes3[ind1[6 * i + 0]].Y(), tmpVertexes3[ind1[6 * i + 0]].Z());
        vertexColors.push_back(tmpColor);
        triangles(tmpTriangleIndex).Set(1, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes2[ind1[6 * i + 1]].X(), tmpVertexes2[ind1[6 * i + 1]].Y(), tmpVertexes2[ind1[6 * i + 1]].Z());
        vertexColors.push_back(tmpColor);
        triangles(tmpTriangleIndex).Set(2, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes2[ind1[6 * i + 2]].X(), tmpVertexes2[ind1[6 * i + 2]].Y(), tmpVertexes2[ind1[6 * i + 2]].Z());
        vertexColors.push_back(tmpColor);
        triangles(tmpTriangleIndex).Set(3, tmpNodeIndex++);

        tmpTriangleIndex++;

        nodes(tmpNodeIndex).SetCoord(tmpVertexes3[ind1[6 * i + 3]].X(), tmpVertexes3[ind1[6 * i + 3]].Y(), tmpVertexes3[ind1[6 * i + 3]].Z());
        vertexColors.push_back(tmpColor);
        triangles(tmpTriangleIndex).Set(1, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes2[ind1[6 * i + 4]].X(), tmpVertexes2[ind1[6 * i + 4]].Y(), tmpVertexes2[ind1[6 * i + 4]].Z());
        vertexColors.push_back(tmpColor);
        triangles(tmpTriangleIndex).Set(2, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes3[ind1[6 * i + 5]].X(), tmpVertexes3[ind1[6 * i + 5]].Y(), tmpVertexes3[ind1[6 * i + 5]].Z());
        vertexColors.push_back(tmpColor);
        triangles(tmpTriangleIndex).Set(3, tmpNodeIndex++);
    }

    Handle(Poly_Triangulation) tmpMesh = new Poly_Triangulation(nodes, triangles);
    TopoDS_Shape* tmpOccShape = new TopoDS_Shape(Mayo::BRepUtils::makeFace(tmpMesh));

    gp_Trsf tmpTrsf;
    occMatToTrsf(mat, tmpTrsf);

    BRepBuilderAPI_Transform xform(*tmpOccShape, tmpTrsf);
    shape = xform.Shape();

    delete tmpOccShape;
    //delete tmpMesh.get();

    return true;
}

bool occCreatePrismaticJointShape(TopoDS_Shape& shape, std::vector<Quantity_Color>& vertexColors, float mat[12], int unit)
{
    gp_Pnt tmpVertexes[9] = { gp_Pnt(0,0,0), 
                              gp_Pnt(-8,8,-70), gp_Pnt(8,8,-70), gp_Pnt(8,-8,-70), gp_Pnt(-8,-8,-70),
                              gp_Pnt(-8,8,70), gp_Pnt(8,8,70), gp_Pnt(8,-8,70), gp_Pnt(-8,-8,70) };

    int ind[36] = {
        1, 2, 3, 1, 3, 4,
        5, 6, 7, 5, 7, 8,
        1, 5, 8, 1, 8, 4,
        2, 6, 7, 2, 7, 3,
        4, 3, 7, 4, 7, 8,
        1, 2, 6, 1, 6, 5
    };

    gp_Pnt tmpVertexes1[9] = { gp_Pnt(0,0,0),
                               gp_Pnt(-4,4,-85), gp_Pnt(4,4,-85), gp_Pnt(4,-4,-85), gp_Pnt(-4,-4,-85),
                               gp_Pnt(-4,4,85), gp_Pnt(4,4,85), gp_Pnt(4,-4,85), gp_Pnt(-4,-4,85) };

    int ind1[36] = {
        1, 2, 3, 1, 3, 4,
        5, 6, 7, 5, 7, 8,
        1, 5, 8, 1, 8, 4,
        2, 6, 7, 2, 7, 3,
        4, 3, 7, 4, 7, 8,
        1, 2, 6, 1, 6, 5
    };

    TColgp_Array1OfPnt nodes(1, 36 * 2);
    Poly_Array1OfTriangle triangles(1, 12 * 2);
    Quantity_Color tmpColor(1, 0, 0, Mayo::TKernelUtils::preferredRgbColorType());
    Quantity_Color tmpColor1(0, 0, 1, Mayo::TKernelUtils::preferredRgbColorType());

    int tmpNodeIndex = 1;
    int tmpTriangleIndex = 1;

    for (int i = 0; i < 12; i++, tmpTriangleIndex++)
    {
        nodes(tmpNodeIndex).SetCoord(tmpVertexes[ind[3 * i + 0]].X(), tmpVertexes[ind[3 * i + 0]].Y(), tmpVertexes[ind[3 * i + 0]].Z());
        vertexColors.push_back(tmpColor);
        triangles(tmpTriangleIndex).Set(1, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes[ind[3 * i + 1]].X(), tmpVertexes[ind[3 * i + 1]].Y(), tmpVertexes[ind[3 * i + 1]].Z());
        vertexColors.push_back(tmpColor);
        triangles(tmpTriangleIndex).Set(2, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes[ind[3 * i + 2]].X(), tmpVertexes[ind[3 * i + 2]].Y(), tmpVertexes[ind[3 * i + 2]].Z());
        vertexColors.push_back(tmpColor);
        triangles(tmpTriangleIndex).Set(3, tmpNodeIndex++);
    }

    for (int i = 0; i < 12; i++, tmpTriangleIndex++)
    {
        nodes(tmpNodeIndex).SetCoord(tmpVertexes1[ind1[3 * i + 0]].X(), tmpVertexes1[ind1[3 * i + 0]].Y(), tmpVertexes1[ind1[3 * i + 0]].Z());
        vertexColors.push_back(tmpColor1);
        triangles(tmpTriangleIndex).Set(1, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes1[ind1[3 * i + 1]].X(), tmpVertexes1[ind1[3 * i + 1]].Y(), tmpVertexes1[ind1[3 * i + 1]].Z());
        vertexColors.push_back(tmpColor1);
        triangles(tmpTriangleIndex).Set(2, tmpNodeIndex++);

        nodes(tmpNodeIndex).SetCoord(tmpVertexes1[ind1[3 * i + 2]].X(), tmpVertexes1[ind1[3 * i + 2]].Y(), tmpVertexes1[ind1[3 * i + 2]].Z());
        vertexColors.push_back(tmpColor1);
        triangles(tmpTriangleIndex).Set(3, tmpNodeIndex++);
    }

    Handle(Poly_Triangulation) tmpMesh = new Poly_Triangulation(nodes, triangles);
    TopoDS_Shape* tmpOccShape = new TopoDS_Shape(Mayo::BRepUtils::makeFace(tmpMesh));

    gp_Trsf tmpTrsf;
    occMatToTrsf(mat, tmpTrsf);

    BRepBuilderAPI_Transform xform(*tmpOccShape, tmpTrsf);
    shape = xform.Shape();

    delete tmpOccShape;
    //delete tmpMesh.get();

    return true;
}

std::string occGetLabelEntry(const TDF_Label& label)
{
    if (label.IsNull())
    {
        return "";
    }

    std::ostringstream oss;
    std::ostream& out = oss;
    label.EntryDump(out);

    return oss.str();
}

TopoDS_Shape occGetShapeFromEntityOwner(const Handle(SelectMgr_EntityOwner)& owner)
{
    static const TopoDS_Shape nullShape;
    auto brepOwner = Handle_StdSelect_BRepOwner::DownCast(owner);
    TopLoc_Location ownerLoc = owner->Location();
#if OCC_VERSION_HEX >= 0x070600
    // Force scale factor to 1
    // If scale factor <> 1 then it will cause a crash(exception) in TopoDS_Shape::Move() starting
    // from OpenCascade >= 7.6
    const double absScale = std::abs(ownerLoc.Transformation().ScaleFactor());
    const double scalePrec = TopLoc_Location::ScalePrec();
    if (absScale < (1. - scalePrec) || absScale >(1. + scalePrec)) {
        gp_Trsf trsf = ownerLoc.Transformation();
        trsf.SetScaleFactor(1.);
        ownerLoc = trsf;
    }
#endif
    return brepOwner ? brepOwner->Shape().Moved(ownerLoc) : nullShape;
}

static QMutex occV3dviewRedrawMutex;

void occV3dviewRedraw(const Handle(V3d_View)& view, bool checkSimStopped)
{
    if (view.IsNull())
    {
        return;
    }

    //if (checkSimStopped)
    //{
    //    int tmpState = simGetSimulationState_internal();
    //    if ((sim_simulation_stopped != tmpState) && (-1 != tmpState))
    //    {
    //        return;
    //    }
    //}

    occV3dviewRedrawMutex.lock();
    view->Redraw();
    occV3dviewRedrawMutex.unlock();

    return;
}

void occV3dviewSetScale(const Handle(V3d_View)& view, float radio, bool checkSimStopped)
{
    if (view.IsNull())
    {
        return;
    }

    //if (checkSimStopped)
    //{
    //    int tmpState = simGetSimulationState_internal();
    //    if ((sim_simulation_stopped != tmpState) && (-1 != tmpState))
    //    {
    //        return;
    //    }
    //}

    occV3dviewRedrawMutex.lock();
    view->SetScale(view->Scale() * radio);
    occV3dviewRedrawMutex.unlock();

    return;
}

void occV3dviewPan(const Handle(V3d_View)& view, int dx, int dy, float theZoomFactor, bool theToStart, bool checkSimStopped)
{
    if (view.IsNull())
    {
        return;
    }

    //if (checkSimStopped)
    //{
    //    int tmpState = simGetSimulationState_internal();
    //    if ((sim_simulation_stopped != tmpState) && (-1 != tmpState))
    //    {
    //        return;
    //    }
    //}

    occV3dviewRedrawMutex.lock();
    view->Pan(dx, dy, theZoomFactor, theToStart);
    occV3dviewRedrawMutex.unlock();

    return;
}

void occV3dviewStartZoomAtPoint(const Handle(V3d_View)& view, int x, int y, bool checkSimStopped)
{
    if (view.IsNull())
    {
        return;
    }

    //if (checkSimStopped)
    //{
    //    int tmpState = simGetSimulationState_internal();
    //    if ((sim_simulation_stopped != tmpState) && (-1 != tmpState))
    //    {
    //        return;
    //    }
    //}

    occV3dviewRedrawMutex.lock();
    view->StartZoomAtPoint(x, y);
    occV3dviewRedrawMutex.unlock();

    return;
}

void occV3dviewZoom(const Handle(V3d_View)& view, int x1, int y1, int x2, int y2, bool checkSimStopped)
{
    if (view.IsNull())
    {
        return;
    }

    //if (checkSimStopped)
    //{
    //    int tmpState = simGetSimulationState_internal();
    //    if ((sim_simulation_stopped != tmpState) && (-1 != tmpState))
    //    {
    //        return;
    //    }
    //}

    occV3dviewRedrawMutex.lock();
    view->Zoom(x1, y1, x2, y2);
    occV3dviewRedrawMutex.unlock();

    return;
}

void occV3dviewZoomAtPoint(const Handle(V3d_View)& view, int x1, int y1, int x2, int y2, bool checkSimStopped)
{
    if (view.IsNull())
    {
        return;
    }

    //if (checkSimStopped)
    //{
    //    int tmpState = simGetSimulationState_internal();
    //    if ((sim_simulation_stopped != tmpState) && (-1 != tmpState))
    //    {
    //        return;
    //    }
    //}

    occV3dviewRedrawMutex.lock();
    view->ZoomAtPoint(x1, y1, x2, y2);
    occV3dviewRedrawMutex.unlock();

    return;
}

void occV3dviewStartRotation(const Handle(V3d_View)& view, int x, int y, bool checkSimStopped)
{
    if (view.IsNull())
    {
        return;
    }

    //if (checkSimStopped)
    //{
    //    int tmpState = simGetSimulationState_internal();
    //    if ((sim_simulation_stopped != tmpState) && (-1 != tmpState))
    //    {
    //        return;
    //    }
    //}

    occV3dviewRedrawMutex.lock();
    view->StartRotation(x, y);
    occV3dviewRedrawMutex.unlock();

    return;
}

void occV3dviewRotation(const Handle(V3d_View)& view, int x, int y, bool checkSimStopped)
{
    if (view.IsNull())
    {
        return;
    }

    //if (checkSimStopped)
    //{
    //    int tmpState = simGetSimulationState_internal();
    //    if ((sim_simulation_stopped != tmpState) && (-1 != tmpState))
    //    {
    //        return;
    //    }
    //}

    occV3dviewRedrawMutex.lock();
    view->Rotation(x, y);
    occV3dviewRedrawMutex.unlock();

    return;
}

void occV3dviewWindowFitAll(const Handle(V3d_View)& view, int xMin, int yMin, int xMax, int yMax, bool checkSimStopped)
{
    if (view.IsNull())
    {
        return;
    }

    //if (checkSimStopped)
    //{
    //    int tmpState = simGetSimulationState_internal();
    //    if ((sim_simulation_stopped != tmpState) && (-1 != tmpState))
    //    {
    //        return;
    //    }
    //}

    occV3dviewRedrawMutex.lock();
    view->WindowFitAll(xMin, yMin, xMax, yMax);
    occV3dviewRedrawMutex.unlock();

    return;
}

void occV3dviewTurn(const Handle(V3d_View)& view, V3d_TypeOfAxe& Axe, Standard_Real Angle, Standard_Boolean Start, bool checkSimStopped)
{
    if (view.IsNull())
    {
        return;
    }

    //if (checkSimStopped)
    //{
    //    int tmpState = simGetSimulationState_internal();
    //    if ((sim_simulation_stopped != tmpState) && (-1 != tmpState))
    //    {
    //        return;
    //    }
    //}

    occV3dviewRedrawMutex.lock();
    view->Turn(Axe, Angle, Start);
    occV3dviewRedrawMutex.unlock();

    return;
}

void occGraphicsSceneHighlightAt(Mayo::GraphicsScene* graphicsScene, int xPos, int yPos, const Handle_V3d_View& view, bool checkSimStopped)
{
    if (NULL == graphicsScene)
    {
        return;
    }

    if (view.IsNull())
    {
        return;
    }

    //if (checkSimStopped)
    //{
    //    int tmpState = simGetSimulationState_internal();
    //    if ((sim_simulation_stopped != tmpState) && (-1 != tmpState))
    //    {
    //        return;
    //    }
    //}

    occV3dviewRedrawMutex.lock();
    graphicsScene->highlightAt(xPos, yPos, view);
    occV3dviewRedrawMutex.unlock();

    return;
}

void occGraphicsScenePick(Mayo::GraphicsScene* graphicsScene, int xPos, int yPos, const Handle_V3d_View& view, bool checkSimStopped)
{
    if (NULL == graphicsScene)
    {
        return;
    }

    if (view.IsNull())
    {
        return;
    }

    //if (checkSimStopped)
    //{
    //    int tmpState = simGetSimulationState_internal();
    //    if ((sim_simulation_stopped != tmpState) && (-1 != tmpState))
    //    {
    //        return;
    //    }
    //}

    auto selector = graphicsScene->mainSelector();

    occV3dviewRedrawMutex.lock();
    selector->Pick(xPos, yPos, view);
    occV3dviewRedrawMutex.unlock();

    return;
}

void occGraphicsSceneAddObject(Mayo::GraphicsScene* graphicsScene, Handle(AIS_InteractiveObject) gfxObj, bool checkSimStopped)
{
    if (NULL == graphicsScene)
    {
        return;
    }

    if (gfxObj.IsNull())
    {
        return;
    }

    //if (checkSimStopped)
    //{
    //    int tmpState = simGetSimulationState_internal();
    //    if ((sim_simulation_stopped != tmpState) && (-1 != tmpState))
    //    {
    //        return;
    //    }
    //}

    occV3dviewRedrawMutex.lock();
    graphicsScene->addObject(gfxObj);
    occV3dviewRedrawMutex.unlock();

    return;
}

void occGraphicsSceneEraseObject(Mayo::GraphicsScene* graphicsScene, Handle(AIS_InteractiveObject) gfxObj, bool checkSimStopped)
{
    if (NULL == graphicsScene)
    {
        return;
    }

    if (gfxObj.IsNull())
    {
        return;
    }

    //if (checkSimStopped)
    //{
    //    int tmpState = simGetSimulationState_internal();
    //    if ((sim_simulation_stopped != tmpState) && (-1 != tmpState))
    //    {
    //        return;
    //    }
    //}

    occV3dviewRedrawMutex.lock();
    graphicsScene->eraseObject(gfxObj);
    occV3dviewRedrawMutex.unlock();

    return;
}

void occGraphicsSceneSetObjectSelect(Mayo::GraphicsScene* graphicsScene, Handle(AIS_InteractiveObject)& gfxObj, bool sel, bool checkSimStopped)
{
    if (NULL == graphicsScene)
    {
        return;
    }

    if (gfxObj.IsNull())
    {
        return;
    }

    //if (checkSimStopped)
    //{
    //    int tmpState = simGetSimulationState_internal();
    //    if ((sim_simulation_stopped != tmpState) && (-1 != tmpState))
    //    {
    //        return;
    //    }
    //}

    occV3dviewRedrawMutex.lock();
    //graphicsScene->toggleOwnerSelection(gfxObj->GlobalSelOwner());
    if (sel)
    {
        if (!(graphicsScene->aisContextPtr()->IsSelected(gfxObj)))
        {
            graphicsScene->toggleOwnerSelection(gfxObj->GlobalSelOwner());
        }
    }
    else
    {
        if (graphicsScene->aisContextPtr()->IsSelected(gfxObj))
        {
            graphicsScene->toggleOwnerSelection(gfxObj->GlobalSelOwner());
        }
    }
    occV3dviewRedrawMutex.unlock();

    return;
}

void occGraphicsSceneSetObjectTransfrom(Mayo::GraphicsScene* graphicsScene, Handle(AIS_InteractiveObject)& gfxObj, const gp_Trsf& trsf, bool checkSimStopped)
{
    if (NULL == graphicsScene)
    {
        return;
    }

    if (gfxObj.IsNull())
    {
        return;
    }

    //if (checkSimStopped)
    //{
    //    int tmpState = simGetSimulationState_internal();
    //    if ((sim_simulation_stopped != tmpState) && (-1 != tmpState))
    //    {
    //        return;
    //    }
    //}

    occV3dviewRedrawMutex.lock();
    graphicsScene->setObjectTransformation(gfxObj, trsf);
    occV3dviewRedrawMutex.unlock();

    return;
}
#endif // 0
