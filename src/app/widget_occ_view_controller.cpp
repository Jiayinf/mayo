/****************************************************************************
** Copyright (c) 2021, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#include "widget_occ_view_controller.h"
#include "widget_occ_view.h"
#include "theme.h"

#include <QtCore/QDebug>
#include <QtCore/QElapsedTimer>
#include <QtGui/QBitmap>
#include <QtGui/QCursor>
#include <QtGui/QPainter>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QRubberBand>
#include <QtWidgets/QStyleFactory>
#include <algorithm>
#include "../occInterfaces/occCommon.h"
#include <gp_Quaternion.hxx>
#include <QRegExpValidator>
#include <QApplication>

#include <Geom_Circle.hxx>
#include <gp_Ax2.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <Geom_Line.hxx>
#include <GC_MakeSegment.hxx>
#include <Prs3d_Arrow.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <gp_Trsf.hxx>
#include <gp_GTrsf.hxx>
#include <math_SVD.hxx>

//#include "SoFCCSysDragger.h"
//using namespace Gui;

namespace Mayo {

    namespace Internal {


        static const QCursor& rotateCursor()
        {
            static QCursor cursor;
            if (!cursor.bitmap()) {
                constexpr int cursorWidth = 16;
                constexpr int cursorHeight = 16;
                constexpr int cursorHotX = 6;
                constexpr int cursorHotY = 8;

                static unsigned char cursorBitmap[] = {
                    0xf0, 0xef, 0x18, 0xb8, 0x0c, 0x90, 0xe4, 0x83,
                    0x34, 0x86, 0x1c, 0x83, 0x00, 0x81, 0x00, 0xff,
                    0xff, 0x00, 0x81, 0x00, 0xc1, 0x38, 0x61, 0x2c,
                    0xc1, 0x27, 0x09, 0x30, 0x1d, 0x18, 0xf7, 0x0f
                };
                static unsigned char cursorMaskBitmap[] = {
                    0xf0, 0xef, 0xf8, 0xff, 0xfc, 0xff, 0xfc, 0xff, 0x3c, 0xfe, 0x1c, 0xff, 0x00, 0xff, 0x00,
                    0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x38, 0x7f, 0x3c, 0xff, 0x3f, 0xff, 0x3f, 0xff, 0x1f,
                    0xf7, 0x0f
                };

                const QBitmap cursorBmp = QBitmap::fromData({ cursorWidth, cursorHeight }, cursorBitmap);
                const QBitmap maskBmp = QBitmap::fromData({ cursorWidth, cursorHeight }, cursorMaskBitmap);
                const QCursor tempCursor(cursorBmp, maskBmp, cursorHotX, cursorHotY);
                cursor = std::move(tempCursor);
            }

            return cursor;
        }

#if OCC_VERSION_HEX >= 0x070600
        using RubberBandWidget_ParentType = QWidget;
#else
        using RubberBandWidget_ParentType = QRubberBand;
#endif

        class RubberBandWidget : public RubberBandWidget_ParentType {
        public:
            RubberBandWidget(QWidget* parent)
#if OCC_VERSION_HEX >= 0x070600
                : RubberBandWidget_ParentType(parent)
            {}
#else
                : RubberBandWidget_ParentType(QRubberBand::Rectangle, parent)
            {
                // QWidget::setStyle() is important, set to windows style will just draw
                // rectangle frame, otherwise will draw a solid rectangle.
                this->setStyle(QStyleFactory::create("windows"));
            }
#endif

        protected:
#if OCC_VERSION_HEX >= 0x070600
            void paintEvent(QPaintEvent*) override
            {
                QPainter painter(this);

                const QColor lineColor = mayoTheme()->color(Theme::Color::RubberBandView3d_Line);
                QColor fillColor = mayoTheme()->color(Theme::Color::RubberBandView3d_Fill);
                fillColor.setAlpha(60);
                QPen pen = painter.pen();
                pen.setColor(lineColor);
                pen.setWidth(2);
                pen.setCapStyle(Qt::FlatCap);
                pen.setJoinStyle(Qt::MiterJoin);

                painter.setPen(pen);
                painter.setBrush(fillColor);
                painter.drawRect(this->rect().adjusted(1, 1, -1, -1));
            }
#endif
        };

    } // namespace Internal

    WidgetOccViewController::WidgetOccViewController(IWidgetOccView* occView, AIS_InteractiveContext* context)
        : QObject(occView->widget()),
        V3dViewController(occView->v3dView()),
        m_occView(occView),
        m_context(context),
        m_navigStyle(View3dNavigationStyle::Catia),
        m_actionMatcher(createActionMatcher(m_navigStyle, &m_inputSequence))
    {
        m_occView->widget()->installEventFilter(this);
        m_inputSequence.setPrePushCallback([=](Input in) { m_actionMatcher->onInputPrePush(in); });
        m_inputSequence.setPreReleaseCallback([=](Input in) { m_actionMatcher->onInputPreRelease(in); });
        m_inputSequence.setClearCallback([=] { m_actionMatcher->onInputCleared(); });
        m_aSequence = new AIS_ManipulatorObjectSequence();
        m_aManipulator = new AIS_Manipulator();

        //SoDB::init();
        //SoFCCSysDragger::initClass(); // 如果是自定义类
        //pcDragger = CoinPtr<SoFCCSysDragger>(new SoFCCSysDragger);

        /*pcDragger->addStartCallback(dragStartCallback, this);
        pcDragger->addFinishCallback(dragFinishCallback, this);
        pcDragger->addMotionCallback(dragMotionCallback, this);*/

        //Handle(Prs3d_DatumAspect) aDatumAspect = new Prs3d_DatumAspect();
        //aDatumAspect->SetAxisLength(100.0, 100.0, 100.0);      // 增大轴长
        //aDatumAspect->SetDatumAttribute(Prs3d_DatumAttribute_ShadingTubeRadius,
        //    3.0
        //);// 增大此值可提升拾取灵敏度);     // 增大箭头
        //aDatumAspect->SetCylinderRadius(2.0);   // 增大旋转圆弧的截面半径（关键！）
        //m_aManipulator->SetDatumAspect(aDatumAspect);

        m_aManipulatorDo = false;
        m_aManipulatorReady = false;
        m_meshId = -1;
    }

    bool WidgetOccViewController::eventFilter(QObject* watched, QEvent* event)
    {
        if (watched != m_occView->widget())
            return false;

        if (event->type() == QEvent::Enter) {
            m_inputSequence.clear();
            m_occView->widget()->grabKeyboard();
            return false;
        }
        else if (event->type() == QEvent::Leave) {
            m_occView->widget()->releaseKeyboard();
            return false;
        }

        this->handleEvent(event);
        return false;
    }

    void WidgetOccViewController::setNavigationStyle(View3dNavigationStyle style)
    {
        m_navigStyle = style;
        m_inputSequence.clear();
        m_actionMatcher = createActionMatcher(style, &m_inputSequence);
    }

    void Mayo::WidgetOccViewController::startManipulator(GraphicsObjectPtr object, float mat[12])
    {
        m_aSequence->Clear();
        m_aSequence->Append(object);

        m_aManipulator->SetPart(0, AIS_ManipulatorMode::AIS_MM_Scaling, Standard_False);  // 禁用了 X 轴的缩放
        m_aManipulator->SetPart(1, AIS_ManipulatorMode::AIS_MM_Scaling, Standard_False);  // 禁用了 Y 轴的缩放
        m_aManipulator->SetPart(2, AIS_ManipulatorMode::AIS_MM_Scaling, Standard_False);  // 禁用了 Z 轴的缩放

        m_attachOption.AdjustPosition = true;
        m_attachOption.AdjustSize = false;
        m_attachOption.EnableModes = false;

        // 将操纵器附在创建的长方体上
        //m_aManipulator->Attach(object, m_attachOption);
        m_aManipulator->Attach(m_aSequence, m_attachOption);

        gp_Ax2 tmpAx2;
        occMatToAx2(mat, tmpAx2, 0);
        m_aManipulator->SetPosition(tmpAx2);

        // 启用指定的操纵模式
        m_aManipulator->EnableMode(AIS_ManipulatorMode::AIS_MM_Translation);  // 启用移动
        m_aManipulator->EnableMode(AIS_ManipulatorMode::AIS_MM_Rotation);     // 启用旋转
        //m_aManipulator->EnableMode(AIS_ManipulatorMode::AIS_MM_Scaling);      // 启用缩放

        // 激活操纵器
        //m_aManipulator->SetModeActivationOnDetection(Standard_True);
    }

    void Mayo::WidgetOccViewController::startManipulator(std::vector<GraphicsObjectPtr>& gfxObjects)
    {
        m_aSequence->Clear();
        for (int i = 0; i < gfxObjects.size(); i++)
        {
            m_aSequence->Append(gfxObjects.at(i));
        }

        // 可以用 SetPart 禁用或启用某些轴的平移、旋转或缩放的可视部分
        m_aManipulator->SetPart(0, AIS_ManipulatorMode::AIS_MM_Scaling, Standard_False);  // 禁用了 X 轴的缩放
        m_aManipulator->SetPart(1, AIS_ManipulatorMode::AIS_MM_Scaling, Standard_False);  // 禁用了 Y 轴的缩放
        m_aManipulator->SetPart(2, AIS_ManipulatorMode::AIS_MM_Scaling, Standard_False);  // 禁用了 Z 轴的缩放
        //m_aManipulator->SetPart(0, AIS_ManipulatorMode::AIS_MM_Rotation, Standard_False); // 禁用了 X 轴的旋转
        //m_aManipulator->SetPart(1, AIS_ManipulatorMode::AIS_MM_Rotation, Standard_False); // 禁用了 Y 轴的旋转
        //m_aManipulator->SetPart(2, AIS_ManipulatorMode::AIS_MM_Rotation, Standard_False); // 禁用了 Z 轴的旋转

        m_attachOption.AdjustPosition = true;
        m_attachOption.AdjustSize = false;
        m_attachOption.EnableModes = false;

        // 将操纵器附在创建的长方体上
        m_aManipulator->Attach(m_aSequence, m_attachOption);

        //// 启用指定的操纵模式
        m_aManipulator->EnableMode(AIS_ManipulatorMode::AIS_MM_Translation);  // 启用移动
        m_aManipulator->EnableMode(AIS_ManipulatorMode::AIS_MM_Rotation);     // 启用旋转
        //m_aManipulator->EnableMode(AIS_ManipulatorMode::AIS_MM_Scaling);      // 启用缩放

        // 激活操纵器
        //m_aManipulator->SetModeActivationOnDetection(Standard_True);
    }

    void Mayo::WidgetOccViewController::setManipulatorReady(bool ready)
    {
        m_aManipulatorReady = ready;
    }

    bool Mayo::WidgetOccViewController::getManipulatorReady()
    {
        return m_aManipulatorReady;
    }

    void Mayo::WidgetOccViewController::stopManipulator()
    {
        // 激活操纵器
        m_aManipulator->SetModeActivationOnDetection(Standard_False);
        //m_aManipulator->DeactivateCurrentMode();
        m_aManipulator->Detach();

        m_meshId = -1;
    }

    gp_Pnt Mayo::WidgetOccViewController::getTransform()
    {
        return m_posTransform;
    }

    // 旋转轨迹可视化
    void Mayo::WidgetOccViewController::ShowRotationTrajectory(const Handle(AIS_InteractiveContext)& ctx,
        const gp_Ax1& rotationAxis,
        double startAngle,
        double endAngle)
    {
        // 在更新轨迹的函数中：
        if (!m_trajectoryShape.IsNull()) {
            ctx->Remove(m_trajectoryShape, Standard_False); // 移除旧轨迹
            m_trajectoryShape.Nullify();
        }

        if (!m_label.IsNull()) {
            ctx->Remove(m_label, Standard_False); // 移除旧轨迹的文字
            m_label.Nullify();
        }

        if (!m_rolabel.IsNull()) {
            ctx->Remove(m_rolabel, Standard_False); // 移除旧轨迹的文字
            m_rolabel.Nullify();
        }

        if (!arrowStart.IsNull()) {
            ctx->Remove(arrowStart, Standard_False); // 移除开始方向箭头
            arrowStart.Nullify();
        }

        if (!arrowEnd.IsNull()) {
            ctx->Remove(arrowEnd, Standard_False); // 移除结束方向箭头
            arrowEnd.Nullify();
        }

        if (std::abs(endAngle - startAngle) <= 1e-6) {
            return;
        }


        tempAngle = endAngle;

        qInfo() << "m_aManipulator->Size()" << m_aManipulator->Size();
        //qInfo() << "m_aManipulator->MyLength()" << m_aManipulator->MyLength();

        // 创建圆弧轨迹
        Handle(Geom_Circle) trajectoryCircle = new Geom_Circle(gp_Ax2(rotationAxis.Location(), rotationAxis.Direction()),
            0.4 * rotationAxis.Location().Distance(m_occView->v3dView().get()->Camera()->Eye()));

        Standard_Real eyeLength = rotationAxis.Location().Distance(m_occView->v3dView().get()->Camera()->Eye());
        qInfo() << "rotationAxis.Location()" << rotationAxis.Location().X() << " , " << rotationAxis.Location().Y() << ", "<< rotationAxis.Location().Z();
        qInfo() << "0.4 * rotationAxis.Location().Distance: " << eyeLength;
        Handle(Geom_TrimmedCurve) trajectoryArc = new Geom_TrimmedCurve(trajectoryCircle, startAngle, endAngle);

        // 获取起始点和终点位置
        gp_Pnt startPoint = trajectoryArc->Value(startAngle);
        gp_Pnt endPoint = trajectoryArc->Value(endAngle);

        // 计算起点箭头方向
        gp_Vec dirStart = gp_Vec(trajectoryArc->Value(startAngle + 0.01), trajectoryArc->Value(startAngle));
        // 计算终点箭头方向（方向要反）
        gp_Vec dirEnd = gp_Vec(trajectoryArc->Value(endAngle - 0.01), trajectoryArc->Value(endAngle));

        if (endAngle < 0) {
            endAngle = 2 * M_PI + endAngle;
            trajectoryArc = new Geom_TrimmedCurve(trajectoryCircle, endAngle, startAngle);
            startPoint = trajectoryArc->Value(endAngle);
            endPoint = trajectoryArc->Value(startAngle);
            dirStart = gp_Vec(trajectoryArc->Value(endAngle + 0.01), trajectoryArc->Value(endAngle));
            dirEnd = gp_Vec(trajectoryArc->Value(startAngle - 0.01), trajectoryArc->Value(startAngle));
        }

        BRepBuilderAPI_MakeEdge edgeMaker(trajectoryArc);

        // 创建显示对象
        m_trajectoryShape = new AIS_Shape(edgeMaker.Edge());

        // 设置显示属性
        m_trajectoryShape->SetColor(Quantity_NOC_GREEN);
        //trajectoryShape->SetTypeOfLine(Aspect_TOL_DASH);
        m_trajectoryShape->SetWidth(5);


        // 显示轨迹
        ctx->Display(m_trajectoryShape, Standard_False);

        // 箭头参数
        Standard_Real arrowLength = 80.0;
        Standard_Real arrowRadius = 40.0;

        dirStart.Normalize();
        gp_Ax2 startAx2(startPoint, gp_Dir(dirStart));
        TopoDS_Shape coneStart = BRepPrimAPI_MakeCone(startAx2, arrowRadius, 0.0, arrowLength);

        dirEnd.Normalize();
        gp_Ax2 endAx2(endPoint, gp_Dir(dirEnd));
        TopoDS_Shape coneEnd = BRepPrimAPI_MakeCone(endAx2, arrowRadius, 0.0, arrowLength);

        // 转为 AIS_Shape 显示
        arrowStart = new AIS_Shape(coneStart);
        arrowStart->SetDisplayMode(AIS_Shaded);
        arrowStart->SetColor(Quantity_NOC_GREEN);
        arrowStart->SetMaterial(Graphic3d_NOM_PLASTIC);
        ctx->Display(arrowStart, Standard_False);

        arrowEnd = new AIS_Shape(coneEnd);
        arrowEnd->SetDisplayMode(AIS_Shaded);
        arrowEnd->SetColor(Quantity_NOC_GREEN);
        arrowEnd->SetMaterial(Graphic3d_NOM_PLASTIC);
        ctx->Display(arrowEnd, Standard_False);

        // 计算圆弧的相关信息
        gp_Pnt circleCenter = trajectoryCircle->Location(); // 圆心位置
        Standard_Real middleAngle = (startAngle + tempAngle) / 2.0; // 计算中间角度
        Standard_Real radius = trajectoryCircle->Radius(); // 圆的半径
        
        // 获取圆弧所在平面的坐标系
        gp_Ax2 axis = trajectoryCircle->Position();
        gp_Dir xDir = axis.XDirection();
        gp_Dir yDir = axis.YDirection();

        // 计算中间点在圆弧上的位置
        gp_Vec xVec(xDir);
        gp_Vec yVec(yDir);
        xVec *= radius * cos(middleAngle);
        yVec *= radius * sin(middleAngle);

        gp_Pnt middlePoint = circleCenter.Translated(xVec + yVec);

        qInfo() << "startAngle: " << startAngle;
        qInfo() << "endAngle: " << endAngle;

        // 在终点显示偏移角度
        Standard_Real distance = (tempAngle - startAngle) * 180.0 / M_PI;
        m_rolabel = new AIS_TextLabel();
        //m_rolabel->SetText((/*"Distance: " + */std::to_string(distance)/* + " mm"*/).c_str());
        m_rolabel->SetText(TCollection_ExtendedString(std::to_string(distance).c_str()));
        //m_rolabel->SetPosition(rotationAxis.Location().XYZ() + rotationAxis.Direction().XYZ() * 1.2);
        m_rolabel->SetPosition(middlePoint.XYZ());
        
        // 关键：放到最顶层
        m_rolabel->SetZLayer(Graphic3d_ZLayerId_Topmost);

        ctx->Display(m_rolabel, Standard_False);

       /* Handle(Geom_Axis1Placement) axisGeom =
            new Geom_Axis1Placement(rotationAxis);
        Handle(AIS_Axis) aisAxis = new AIS_Axis(axisGeom);
        ctx->Display(aisAxis, Standard_True);*/

    }

    void Mayo::WidgetOccViewController::ShowTransformTrajectory(const Handle(AIS_InteractiveContext)& ctx, const gp_Ax1& rotationAxis, gp_Pnt startPoint, gp_Pnt endPoint)
    {
        // 在更新轨迹的函数中：
        if (!m_trajectoryShape.IsNull()) {
            ctx->Remove(m_trajectoryShape, Standard_False); // 移除旧轨迹
            m_trajectoryShape.Nullify();
        }

        if (!m_label.IsNull()) {
            ctx->Remove(m_label, Standard_False); // 移除旧轨迹的文字
            m_label.Nullify();
        }

        if (!m_rolabel.IsNull()) {
            ctx->Remove(m_rolabel, Standard_False); // 移除旧轨迹的文字
            m_rolabel.Nullify();
        }

        if (!arrowStart.IsNull()) {
            ctx->Remove(arrowStart, Standard_False); // 移除开始方向箭头
            arrowStart.Nullify();
        }

        if (!arrowEnd.IsNull()) {
            ctx->Remove(arrowEnd, Standard_False); // 移除结束方向箭头
            arrowEnd.Nullify();
        }

        // 创建直线几何
        if (endPoint.IsEqual(startPoint, 1e-6)) {
            return;
        }
        TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(startPoint, endPoint);
        Handle(AIS_Shape) lineShape = new AIS_Shape(edge);

        //// 在轨迹末端添加箭头
        //gp_Vec direction(startPoint, endPoint);
        //gp_Pnt lastPos = endPoint;

        //BRepPrimAPI_MakeCone mkCone(gp_Ax2(endPoint, direction), 0.005, 0, 0.02);

        //TopoDS_Shape shapeAll = BRepAlgoAPI_Fuse(edge, mkCone.Shape());

        m_trajectoryShape = lineShape;

        m_trajectoryShape->SetColor(Quantity_NOC_BLACK);
        m_trajectoryShape->SetWidth(5);
        ctx->Display(m_trajectoryShape, Standard_False);

        

        //// 新增：用“沿操纵器轴方向的投影”作为带符号距离
        //const gp_Vec v(startPoint, endPoint);                 // 位移向量
        //const gp_Vec axisVec(rotationAxis.Direction());

        //// signedDistance 是“沿轴方向”的带符号位移
        //// >0: 沿轴正向；<0: 沿轴反向
        //Standard_Real signedDistance = v.Dot(axisVec);


        //// 在终点显示移动距离
        ///*Standard_Real distance = startPoint.Distance(endPoint);*/

        ////if ((std::abs(endPoint.X() - startPoint.X()) > 1e-6 && endPoint.X() < startPoint.X()) ||
        ////    (std::abs(endPoint.Y() - startPoint.Y()) > 1e-6 && endPoint.Y() < startPoint.Y()) ||
        ////    (std::abs(endPoint.Z() - startPoint.Z()) > 1e-6 && endPoint.Z() < startPoint.Z())) {
        //////if (endPoint.X() < startPoint.X() || endPoint.Y() < startPoint.Y() || endPoint.Z() < startPoint.Z()) {
        ////    distance = - distance; 
        ////}
        //m_label = new AIS_TextLabel();
        //std::string distanceStr = "Distance: " + std::to_string(signedDistance) + " mm";
        //m_label->SetText(TCollection_ExtendedString(distanceStr.c_str()));

        const gp_Vec v(startPoint, endPoint);
        const gp_Vec axisVec(rotationAxis.Direction());
        Standard_Real signedDistance = v.Dot(axisVec);

        // 避免 -0.000000 之类的显示
        if (std::abs(signedDistance) < 1e-9) {
            signedDistance = 0.0;
        }

        // 按固定小数位输出，避免 std::to_string 冗长
        std::ostringstream oss;
        oss.setf(std::ios::fixed);
        oss << std::setprecision(3) << signedDistance;
        std::string distanceStr = "Distance: " + oss.str() + " mm";

        m_label = new AIS_TextLabel();
        m_label->SetText(TCollection_ExtendedString(distanceStr.c_str()));




        gp_Pnt midPoint((startPoint.XYZ() + endPoint.XYZ()) / 2.0);
       /* qInfo() << "ShowTransformTrajectory midPoing (x,y,z):" << midPoint.X() << ", " j << midPoint.Y() << ", " << midPoint.Z();
        qInfo() << "ShowTransformTrajectory startPoint(X,Y,Z):" << startPoint.X() << " , " << startPoint.Y() << " , " << startPoint.Z() << " , ";
        qInfo() << "ShowTransformTrajectory endPoint(X,Y,Z):" << endPoint.X() << " , " << endPoint.Y() << " , " << endPoint.Z() << " , ";*/
        m_label->SetPosition(midPoint);

		// 关键：在 Display 之前设置 ZLayer，并放到最顶层
        m_label->SetZLayer(Graphic3d_ZLayerId_Topmost);

        ctx->Display(m_label, Standard_False);

        // 算方向向量
        gp_Vec vec(startPoint, endPoint);
        if (vec.Magnitude() < 1e-6) return; // 太短则不显示箭头
        gp_Dir dir(vec);
        gp_Dir revDir = dir.Reversed();

        // 箭头参数
        Standard_Real arrowLength = 80.0;
        Standard_Real arrowRadius = 40.0;

        // 终点箭头
        gp_Pnt arrowEndPoint = endPoint.Translated(gp_Vec(revDir) * (arrowLength - 20));
        gp_Ax2 endAx2(arrowEndPoint, dir); // 从终点朝向外
        TopoDS_Shape coneEnd = BRepPrimAPI_MakeCone(endAx2, arrowRadius, 0.0, arrowLength);
        arrowEnd = new AIS_Shape(coneEnd);
        arrowEnd->SetDisplayMode(AIS_Shaded);
        arrowEnd->SetMaterial(Graphic3d_NOM_PLASTIC);
        arrowEnd->SetColor(Quantity_NOC_BLACK);
        ctx->Display(arrowEnd, false);

        // 起点箭头
        gp_Pnt arrowStartPoint = startPoint.Translated(gp_Vec(dir) * (arrowLength - 20));
        gp_Ax2 startAx2(arrowStartPoint, revDir); // 从起点朝向外
        TopoDS_Shape coneStart = BRepPrimAPI_MakeCone(startAx2, arrowRadius, 0.0, arrowLength);
        arrowStart = new AIS_Shape(coneStart);
        arrowStart->SetDisplayMode(AIS_Shaded);
        arrowStart->SetMaterial(Graphic3d_NOM_PLASTIC);
        arrowStart->SetColor(Quantity_NOC_BLACK);
        ctx->Display(arrowStart, false);
    }

    void WidgetOccViewController::redrawView()
    {
        //V3dViewController::redrawView();
        m_occView->redraw();
    }

    void WidgetOccViewController::startDynamicAction(V3dViewController::DynamicAction action)
    {
        if (action == DynamicAction::Rotation)
            this->setViewCursor(Internal::rotateCursor());
        else if (action == DynamicAction::Panning)
            this->setViewCursor(Qt::SizeAllCursor);
        else if (action == DynamicAction::Zoom)
            this->setViewCursor(Qt::SizeVerCursor);
        else if (action == DynamicAction::WindowZoom)
            this->setViewCursor(Qt::SizeBDiagCursor);

        V3dViewController::startDynamicAction(action);
    }

    void WidgetOccViewController::stopDynamicAction()
    {
        this->setViewCursor(Qt::ArrowCursor);
        V3dViewController::stopDynamicAction();
    }

    void WidgetOccViewController::setViewCursor(const QCursor& cursor)
    {
        if (m_occView->widget())
            m_occView->widget()->setCursor(cursor);
    }

    struct WidgetOccViewController::RubberBand : public V3dViewController::IRubberBand {
        RubberBand(QWidget* parent)
            : m_rubberBand(parent)
        {
        }

        void updateGeometry(int x, int y, int width, int height) override {
            m_rubberBand.setGeometry(x, y, width, height);
        }

        void setVisible(bool on) override {
            m_rubberBand.setVisible(on);
        }

    private:
        Internal::RubberBandWidget m_rubberBand;
    };

    std::unique_ptr<V3dViewController::IRubberBand> WidgetOccViewController::createRubberBand()
    {
        return std::make_unique<RubberBand>(m_occView->widget());
    }

    void WidgetOccViewController::handleEvent(const QEvent* event)
    {
        switch (event->type()) {
        case QEvent::KeyPress:
            this->handleKeyPress(static_cast<const QKeyEvent*>(event));
            break;
        case QEvent::KeyRelease:
            this->handleKeyRelease(static_cast<const QKeyEvent*>(event));
            break;
        case QEvent::MouseButtonPress:
            this->handleMouseButtonPress(static_cast<const QMouseEvent*>(event));
            break;
        case QEvent::MouseMove:
            this->handleMouseMove(static_cast<const QMouseEvent*>(event));
            break;
        case QEvent::MouseButtonRelease:
            this->handleMouseButtonRelease(static_cast<const QMouseEvent*>(event));
            break;
        case QEvent::Wheel:
            this->handleMouseWheel(static_cast<const QWheelEvent*>(event));
            break;
        default:
            break;
        } // end switch
    }

    void WidgetOccViewController::handleKeyPress(const QKeyEvent* event)
    {
        if (event->isAutoRepeat())
            return;

        QEvent* passEvent = const_cast<QEvent*>(static_cast<const QEvent*>(event));
        // 如果 m_editLine 有焦点，将事件转发给它
        if (m_editLine && m_editLine->hasFocus()) {
            QApplication::sendEvent(m_editLine, passEvent);  // 将事件发送给 m_editLine
            return;  // 阻止事件继续传播
        }

        m_inputSequence.push(event->key());
        if (m_inputSequence.equal({ Qt::Key_Space }))
            this->startInstantZoom(toPosition(m_occView->widget()->mapFromGlobal(QCursor::pos())));

        if (m_inputSequence.equal({ Qt::Key_Shift }) && !this->hasCurrentDynamicAction())
            this->signalMultiSelectionToggled.send(true);
    }

    void WidgetOccViewController::handleKeyRelease(const QKeyEvent* event)
    {
        if (event->isAutoRepeat())
            return;

        QEvent* passEvent = const_cast<QEvent*>(static_cast<const QEvent*>(event));
        // 如果 m_editLine 有焦点，将事件转发给它
        if (m_editLine && m_editLine->hasFocus()) {
            QApplication::sendEvent(m_editLine, passEvent);  // 将事件发送给 m_editLine
            return;  // 阻止事件继续传播
        }

        m_inputSequence.release(event->key());
        if (!m_inputSequence.equal({}))
            return;

        if (m_inputSequence.lastInput() == Qt::Key_Space && this->currentDynamicAction() == DynamicAction::InstantZoom)
            this->stopInstantZoom();

        if (m_inputSequence.lastInput() == Qt::Key_Shift && !this->hasCurrentDynamicAction())
            this->signalMultiSelectionToggled.send(false);
    }

    void WidgetOccViewController::handleMouseButtonPress(const QMouseEvent* event)
    {
                
        m_inputSequence.push(event->button());

   /*    if (m_context && !m_label.IsNull()) {

            m_context->InitSelected();
            if (m_context->MoreSelected()) {
                const Handle(AIS_InteractiveObject)& selected = m_context->SelectedInteractive();
                if (selected == m_label) {
                }

            }

        }*/

        /*if (m_editLine && (m_editLine->hasFocus() || m_editLine->text() != "")) {

            return;
        }*/

        const QPoint currPos = m_occView->widget()->mapFromGlobal(event->globalPos());
        m_prevPos = toPosition(currPos);

        if (m_aManipulatorReady)
        {
            int currentOperation = -1;
            m_aManipulatorDo = true;
            m_aManipulator->SetModeActivationOnDetection(Standard_True);

            bool ret = m_aManipulator->IsAttached();
            int mode = m_aManipulator->ActiveAxisIndex();

            if (m_aManipulatorDo && m_aManipulator->HasActiveMode())
            {
                int tmpActiveAxisIndex = m_aManipulator->ActiveAxisIndex();
                int tmpActiveAxisMode = m_aManipulator->ActiveMode();

                //m_aManipulator->GetOwner();

                //m_initialPosition = m_aManipulator->Position().Location();
                //m_initialRotation = m_aManipulator->Transformation();
                // 之前通过设置距离移动过需要恢复
                /*if (setMoveLine) {
                    tmpActiveAxisIndex = activeAxisIndexTmp;
                    tmpActiveAxisMode = activeModeTmp;
                }*/

                if (AIS_MM_Translation == tmpActiveAxisMode)
                {

                    if (0 == tmpActiveAxisIndex)
                    {
                        m_aManipulator->SetPart(1, AIS_ManipulatorMode::AIS_MM_Translation, Standard_False); // 禁用了 Y 轴的平移
                        m_aManipulator->SetPart(2, AIS_ManipulatorMode::AIS_MM_Translation, Standard_False); // 禁用了 Z 轴的平移
                        currentOperation = 1;
                    }
                    else if (1 == tmpActiveAxisIndex)
                    {
                        m_aManipulator->SetPart(0, AIS_ManipulatorMode::AIS_MM_Translation, Standard_False); // 禁用了 X 轴的平移
                        m_aManipulator->SetPart(2, AIS_ManipulatorMode::AIS_MM_Translation, Standard_False); // 禁用了 Z 轴的平移
                        currentOperation = 2;
                    }
                    else if (2 == tmpActiveAxisIndex)
                    {
                        m_aManipulator->SetPart(0, AIS_ManipulatorMode::AIS_MM_Translation, Standard_False); // 禁用了 X 轴的平移
                        m_aManipulator->SetPart(1, AIS_ManipulatorMode::AIS_MM_Translation, Standard_False); // 禁用了 Y 轴的平移
                        currentOperation = 3;
                    }

                    m_aManipulator->SetPart(0, AIS_ManipulatorMode::AIS_MM_Rotation, Standard_False); // 禁用了 X 轴的旋转
                    m_aManipulator->SetPart(1, AIS_ManipulatorMode::AIS_MM_Rotation, Standard_False); // 禁用了 Y 轴的旋转
                    m_aManipulator->SetPart(2, AIS_ManipulatorMode::AIS_MM_Rotation, Standard_False); // 禁用了 Z 轴的旋转
                }
                else if (AIS_MM_Rotation == tmpActiveAxisMode)
                {
                    if (0 == tmpActiveAxisIndex)
                    {
                        m_aManipulator->SetPart(1, AIS_ManipulatorMode::AIS_MM_Rotation, Standard_False); // 禁用了 Y 轴的旋转
                        m_aManipulator->SetPart(2, AIS_ManipulatorMode::AIS_MM_Rotation, Standard_False); // 禁用了 Z 轴的旋转
                        currentOperation = 4;
                        m_axis = gp_XYZ(1, 0, 0);
                        //m_aManipulator->SetPosition(gp_Ax2(m_aManipulator->Position().Location(), m_aManipulator->Position().XDirection()));
                    }
                    else if (1 == tmpActiveAxisIndex)
                    {
                        m_aManipulator->SetPart(0, AIS_ManipulatorMode::AIS_MM_Rotation, Standard_False); // 禁用了 X 轴的旋转
                        m_aManipulator->SetPart(2, AIS_ManipulatorMode::AIS_MM_Rotation, Standard_False); // 禁用了 Z 轴的旋转
                        currentOperation = 5;
                        m_axis = gp_XYZ(0, 1, 0);
                        //m_aManipulator->SetPosition(gp_Ax2(m_aManipulator->Position().Location(), m_aManipulator->Position().YDirection()));
                    }
                    else if (2 == tmpActiveAxisIndex)
                    {
                        m_aManipulator->SetPart(0, AIS_ManipulatorMode::AIS_MM_Rotation, Standard_False); // 禁用了 X 轴的旋转
                        m_aManipulator->SetPart(1, AIS_ManipulatorMode::AIS_MM_Rotation, Standard_False); // 禁用了 Y 轴的旋转
                        currentOperation = 6;
                        m_axis = gp_XYZ(0, 0, 1);
                        //m_aManipulator->SetPosition(gp_Ax2(m_aManipulator->Position().Location(), m_aManipulator->Position().XDirection().Crossed(m_aManipulator->Position().YDirection())));
                    }

                    m_aManipulator->SetPart(0, AIS_ManipulatorMode::AIS_MM_Translation, Standard_False); // 禁用了 X 轴的旋转
                    m_aManipulator->SetPart(1, AIS_ManipulatorMode::AIS_MM_Translation, Standard_False); // 禁用了 Y 轴的旋转
                    m_aManipulator->SetPart(2, AIS_ManipulatorMode::AIS_MM_Translation, Standard_False); // 禁用了 Z 轴的旋转
                }

                if (m_lastOperation == -1)
                {
                    m_lastOperation = currentOperation;
                    m_initialPosition = m_aManipulator->Position().Location();
                    m_initialRotation = m_aManipulator->Transformation();
                    //totalAngle = 0;
                }
                else
                {
                    if (currentOperation == m_lastOperation)
                    {
                        //不需要更新
                    }
                    else
                    {
                        m_initialPosition = m_aManipulator->Position().Location();
                        m_initialRotation = m_aManipulator->Transformation();
                        //totalAngle = 0;
                        m_lastOperation = currentOperation;
                    }
                }

                if (0 <= tmpActiveAxisIndex)
                {
                    gp_Ax2 tmpAx21 = m_aManipulator->Position();
                    m_aManipulator->Attach(m_aSequence, m_attachOption);

                    if (-1 != m_meshId)
                    {
                        float tmpMat[12] = { 0 };
                        //simGetObjectMatrix_internal(m_meshId, -1, tmpMat);
                        gp_Ax2 tmpAx2;
                        occMatToAx2(tmpMat, tmpAx2);
                        m_aManipulator->SetPosition(tmpAx2);
                    }
                    else
                    {
                        m_aManipulator->SetPosition(tmpAx21);
                    }

                    m_aManipulator->StartTransform(currPos.x(), currPos.y(), m_occView->v3dView());	// 初始化转换，记录起始位置
                }
            }
        }
    }

    void WidgetOccViewController::handleMouseMove(const QMouseEvent* event)
    {

        /*if (m_editLine && (m_editLine->hasFocus() || m_editLine->text() != "")) {

            return;
        }*/

        const Position currPos = toPosition(m_occView->widget()->mapFromGlobal(event->globalPos()));
        const Position prevPos = m_prevPos;
        m_prevPos = currPos;
        if (m_actionMatcher->matchRotation())
        {
            if (m_actionMatcher->matchRotation())
            {
                if (!m_aManipulatorReady || !m_aManipulatorDo)
                {
                    this->rotation(currPos);
                }
                else
                {
                    int tmpActiveAxisIndex = m_aManipulator->ActiveAxisIndex();
                    if (0 <= tmpActiveAxisIndex)
                    {
                        if (m_aManipulator->HasActiveMode())
                        {
                            m_aManipulator->Transform(currPos.x, currPos.y, m_occView->v3dView()); // 应用鼠标从起始位置开始移动而产生的变换

                            m_lastManipulatorPos = currPos;

                            static auto lastTime = std::chrono::high_resolution_clock::now();
                            auto curTime = std::chrono::high_resolution_clock::now();
                            auto durationTime = std::chrono::duration_cast<std::chrono::milliseconds>(curTime - lastTime);
                            if (100 <= durationTime.count())
                            {
                                lastTime = curTime;
                                //emit CGlobalEventSender::getInstance()->occManipulatorMoved();
                            }

                            redrawView();
                            int tmpActiveAxisMode = m_aManipulator->ActiveMode();

                            if (AIS_MM_Translation == tmpActiveAxisMode)
                            {
                                if (m_aManipulator->HasTransformation())
                                {
                                    gp_Pnt currentPosition = m_aManipulator->Position().Location();
                                    // 获取平移值
                                    gp_Vec displacement(m_initialPosition, currentPosition);
                                    std::cout << "Translation: ("
                                        << displacement.X() << ", "
                                        << displacement.Y() << ", "
                                        << displacement.Z() << ")" << std::endl;

                                    m_posTransform.SetX(displacement.X() * 1000);
                                    m_posTransform.SetY(displacement.Y() * 1000);
                                    m_posTransform.SetZ(displacement.Z() * 1000);

                                    // 新增：平移模式下：给 ShowTransformTrajectory 传入“当前激活轴”的方向
                                    // 取操纵器当前坐标系（世界坐标下）
                                    const gp_Ax2 manipAx2 = m_aManipulator->Position();

                                    // 根据 ActiveAxisIndex 取对应方向（世界坐标下的单位方向）
                                    gp_Dir axisDir;
                                    if (tmpActiveAxisIndex == 0) {
                                        axisDir = manipAx2.XDirection();       // X
                                    }
                                    else if (tmpActiveAxisIndex == 1) {
                                        axisDir = manipAx2.YDirection();       // Y
                                    }
                                    else { // tmpActiveAxisIndex == 2
                                        axisDir = manipAx2.Direction();        // Z（gp_Ax2::Direction() 是主方向）
                                    }

                                    // 用初始位置 + 轴方向构造 gp_Ax1
                                    const gp_Ax1 axis1(m_initialPosition, axisDir);
                                    
                                    /*gp_Ax1 axis1;*/
                                    ShowTransformTrajectory(m_context, axis1, m_initialPosition, currentPosition);
                                }
                            }
                            else if (AIS_MM_Rotation == tmpActiveAxisMode)
                            {
                                gp_Pnt currentPosition = m_aManipulator->Position().Location();

                                gp_Trsf currentRotation = m_aManipulator->Transformation();

                                //{
                                //    // 提取旋转四元数
                                //    gp_Quaternion quat = currentRotation.GetRotation();

                                //    // 转换为轴-角表示
                                //    gp_Vec axisVec;
                                //    Standard_Real angle;
                                //    quat.GetVectorAndAngle(axisVec, angle);
                                //    int a = 0;
                                //    // 检查旋转轴是否匹配
                                //    //if (axisVec.IsParallel(axis.Direction(), Precision::Angular())) {
                                //    //    return; // 返回弧度值
                                //    //}
                                //}

                                //{
                                //    //test
                                //    // 获取旋转轴和角度
                                //    gp_XYZ axis;
                                //    Standard_Real angle;
                                //    currentRotation.GetRotation(axis, angle);

                                //    if (gp_Dir(axis).Dot(m_axis) < 0)
                                //    {
                                //        axis.Reverse();
                                //        m_axis = axis;
                                //        //angle = -angle;
                                //        //currentRotation.SetRotation(m_axis);
                                //    }

                                //    gp_XYZ axis2;
                                //    Standard_Real angle2;
                                //    m_initialRotation.GetRotation(axis2, angle2);
                                //    if (currentRotation.Form() != gp_Rotation) {
                                //        // 不是纯旋转变换
                                //        int b = currentRotation.Form();

                                //        gp_Mat matrix2 = currentRotation.VectorialPart();



                                //        int c = 0;
                                //    }

                                //    /* currentRotation.SetRotation(axis, angle);*/

                                //    if (m_initialRotation.Form() != gp_Rotation) {
                                //        // 不是纯旋转变换
                                //        int b = m_initialRotation.Form();
                                //        int c = 0;
                                //    }

                                //    int a = 0;
                                //}




                                gp_Quaternion deltaRotation = currentRotation.GetRotation() * m_initialRotation.GetRotation().Inverted();
                                //if (deltaRotation.W() < 0) deltaRotation = -deltaRotation; // 统一为 w > 0 的表示
                                static Standard_Real lastAngle = 0.0;
                                gp_Vec axis;
                                Standard_Real angle;
                                deltaRotation.GetVectorAndAngle(axis, angle);

                                qInfo() << "axis: " << axis.X() << " , " << axis.Y() << " , " << axis.Z();
                                qInfo() << "angle: " << angle;
                                qInfo() << "deltaRotation.W(): " << deltaRotation.W();


                                //if (angle < 0) angle += 2 * M_PI;

                                //if (std::abs(angle - lastAngle) > M_PI) 
                                //{
                                //    /*angle += (angle > 0) ? -2 * M_PI : 2 * M_PI;*/
                                //    if (angle > 0)
                                //    {
                                //        angle = 2 * M_PI - angle;
                                //    }
                                //    else
                                //    {
                                //        angle = 2 * M_PI + angle;
                                //    }
                                //}
                                //lastAngle = angle;

                                // 1. 获取局部坐标系和旋转轴
                                gp_Ax2 localAxes = m_aManipulator->Position();
                                gp_Dir rotationAxis;
                                gp_Dir initialDir;
                                if (tmpActiveAxisIndex == 0)
                                {
                                    rotationAxis = localAxes.XDirection(); // 当前旋转轴
                                    initialDir = localAxes.YDirection();  // 初始参考方向
                                }
                                else if (tmpActiveAxisIndex == 1)
                                {
                                    rotationAxis = localAxes.YDirection(); // 当前旋转轴
                                    initialDir = localAxes.Direction();  // 初始参考方向
                                }
                                else if (tmpActiveAxisIndex == 2)
                                {
                                    rotationAxis = localAxes.Direction(); // 当前旋转轴
                                    initialDir = localAxes.XDirection();  // 初始参考方向
                                }
                                //gp_Dir rotationAxis = localAxes.Direction(); // 当前旋转轴
                                //gp_Dir initialDir = localAxes.XDirection();  // 初始参考方向

                                //// 2. 计算当前方向向量（投影到旋转平面）
                                //gp_Pnt center = localAxes.Location();
                                //gp_Vec currentVec(center, m_initialPosition);
                                //gp_Vec normalVec(rotationAxis);
                                //currentVec -= normalVec * (currentVec.Dot(normalVec));

                                //// 3. 计算角度
                                //gp_Vec initVec(initialDir);
                                //initVec.Normalize();
                                //currentVec.Normalize();

                                //Standard_Real angle = initVec.Angle(currentVec);
                                //gp_Vec cross = initVec.Crossed(currentVec);
                                //if (cross.Dot(normalVec) < 0) {
                                //    angle = -angle;
                                //}

                                //static Standard_Real totalAngle = 0.0;
                                //totalAngle += angle;

        /*                        Standard_Real angle = xDir1.AngleWithRef(xDir2, zAxis1);*/



                                //// 假设激活的是Z轴旋转（根据实际情况调整）
                                //rotationAxis = m_aManipulator->Position().Axis(); // 0:X, 1:Y, 2:Z

                                m_posTransform.SetX(axis.X());
                                m_posTransform.SetY(axis.Y());
                                m_posTransform.SetZ(angle * 180.0 / M_PI);

                                // 1) 当前操纵器选中的旋转轴
                                gp_Vec axisRotation;
                                if (0 == tmpActiveAxisIndex)
                                {
                                    axisRotation = m_aManipulator->Position().XDirection();
                                }
                                else if (1 == tmpActiveAxisIndex)
                                {
                                    axisRotation = m_aManipulator->Position().YDirection();
                                }
                                else if (2 == tmpActiveAxisIndex)
                                {
                                    axisRotation = m_aManipulator->Position().XDirection().Crossed(m_aManipulator->Position().YDirection());
                                }
                                //gp_Ax1 axis1(currentPosition, axisRotation);
                                //tmpRotationAxis = axis1;

                                axisRotation.Normalize();

                                // 2) deltaRotation 轴角分解得到的 axis/angle
                                gp_Vec deltaAxis = axis;
                                if (deltaAxis.Magnitude() > 1e-12) {
                                    deltaAxis.Normalize();
                                }

                                // 3) 用 dot 决定符号（绑定到操纵器轴，不看世界坐标分量）
                                double signedAngle = angle;
                                if (deltaAxis.Dot(axisRotation) < 0.0) {
                                    signedAngle = -angle;
                                }

                                // 4) 直接把 signedAngle 交给 ShowRotationTrajectory
                                gp_Ax1 axis1(currentPosition, axisRotation);
                                ShowRotationTrajectory(m_context, axis1, 0.0, signedAngle);




                                //if ((std::abs(axis.X()) > 1e-6 && axis.X() < 0) ||
                                //    (std::abs(axis.Y()) > 1e-6 && axis.Y() < 0) ||
                                //    (std::abs(axis.Z()) > 1e-6 && axis.Z() < 0)) {
                                //    ShowRotationTrajectory(m_context, axis1, 0.0, -angle);
                                //}
                                //else {
                                //    ShowRotationTrajectory(m_context, axis1, 0.0, angle);
                                //}
                                //ShowRotationTrajectory(m_context, axis1, 0.0, angle);
                                /*startAngle = angle;*/
                            }

                        }
                    }
                    else
                    {
                        this->rotation(currPos);
                    }
                }
            }
        }
        //this->rotation(currPos);
        else if (m_actionMatcher->matchPan())
            this->pan(prevPos, currPos);
        else if (m_actionMatcher->matchZoom())
            this->zoom(prevPos, currPos);
        else if (m_actionMatcher->matchWindowZoom())
            this->windowZoomRubberBand(currPos);
        else
            this->signalMouseMoved.send(currPos.x, currPos.y);
    }

    void WidgetOccViewController::handleMouseButtonRelease(const QMouseEvent* event)
    {
        auto fnOccMouseBtn = [](Qt::MouseButton btn) -> Aspect_VKeyMouse {
            switch (btn) {
            case Qt::NoButton: return Aspect_VKeyMouse_NONE;
            case Qt::LeftButton: return Aspect_VKeyMouse_LeftButton;
            case Qt::RightButton: return Aspect_VKeyMouse_RightButton;
            case Qt::MiddleButton: return Aspect_VKeyMouse_MiddleButton;
            default: return Aspect_VKeyMouse_UNKNOWN;
            }
            };        

        m_inputSequence.release(event->button());
        const bool hadDynamicAction = this->hasCurrentDynamicAction();
        if (this->isWindowZoomingStarted())
            this->windowZoom(toPosition(m_occView->widget()->mapFromGlobal(event->globalPos())));

        this->stopDynamicAction();
        if (!hadDynamicAction)
            this->signalMouseButtonClicked.send(fnOccMouseBtn(event->button()));

        if (m_context && (!m_label.IsNull() || !m_rolabel.IsNull())) {


            m_context->InitSelected();
            if (m_context->MoreSelected()) {
                const Handle(AIS_InteractiveObject)& selected = m_context->SelectedInteractive();
                if (selected == m_label)
                {
                    QString currentText = QString::fromUtf16(m_label->Text().ToExtString());

                    QWidget* parentWidget = m_occView->widget()->parentWidget();  // WidgetGuiDocument

                    if (!m_editLine) {
                        m_editLine = new QLineEdit(parentWidget); // 覆盖在 viewer 上
                        setMoveLine = true;
                        //m_editLine = new QLineEdit(nullptr);  // 没有父控件，系统浮动窗口
                        //m_editLine->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
                        m_editLine->setStyleSheet("background: white; color: black; border: 1px solid red;");
                        m_editLine->setAlignment(Qt::AlignCenter);
                        m_editLine->setValidator(new QRegularExpressionValidator(QRegularExpression("^-?(0|([1-9][0-9]*))(\\.[0-9]+)?$")));
                        m_editLine->resize(150, 24);
                        m_editLine->setFrame(true);
                        m_editLine->hide();
                        

                        // 设置文本框位置
                        QStringList qlist = currentText.split(" ");
                        m_editLine->setText(qlist[1]);

                        /*QPoint editPoint;
                        editPoint.setX(QCursor::pos().x());
                        editPoint.setY(QCursor::pos().y() - 50);
                        m_editLine->move(editPoint);*/
                        //qInfo() << "QCursor::pos():" << QCursor::pos();

                        // 1) global -> parentWidget local
                        const QPoint globalPos = QCursor::pos();
                        QPoint localPos = parentWidget->mapFromGlobal(globalPos);

                        // 2) 原来是 y-50，这里保持同样 向上偏移
                        localPos += QPoint(0, -50);

                        //// 3) 边界裁剪，避免出界
                        //const QSize sz = m_editLine->size();
                        //localPos.setX(std::clamp(localPos.x(), 0, parentWidget->width() - sz.width()));
                        //localPos.setY(std::clamp(localPos.y(), 0, parentWidget->height() - sz.height()));

                        m_editLine->move(localPos);


                        m_editLine->show();       // 显示
                        m_editLine->raise();      // 放到最上层
                        m_editLine->setFocusPolicy(Qt::StrongFocus);
                        m_editLine->setFocus();   // 获得焦点

                        //qInfo() << "handleMouseButtonRelease create m_label!!!";


                        connect(m_editLine, &QLineEdit::editingFinished, this, [this]() {
                            const QString distanceText = m_editLine->text().trimmed();
                            if (distanceText.isEmpty() || m_label.IsNull()) {
                                delete m_editLine;
                                m_editLine = nullptr;
                                return;
                            }

                            bool ok = false;
                            const double distanceMm = distanceText.toDouble(&ok);
                            if (!ok) {
                                delete m_editLine;
                                m_editLine = nullptr;
                                return;
                            }

                            // ① 当前 manipulator 姿态 & 轴方向（严格沿当前激活轴）
                            gp_Ax2 curAx2 = m_aManipulator->Position();
                            const int axisIndex = m_aManipulator->ActiveAxisIndex();

                            gp_Dir axisDir;
                            if (axisIndex == 0)      axisDir = curAx2.XDirection();
                            else if (axisIndex == 1) axisDir = curAx2.YDirection();
                            else                     axisDir = curAx2.Direction(); // Z 轴

                            // ② 起点用拖动的几何起点，不再从 label 中点反推
                            const gp_Pnt startPoint = m_initialPosition;

                            
                            const Standard_Real distanceModel = distanceMm;
                            gp_Pnt newEndPoint = startPoint.Translated(gp_Vec(axisDir) * distanceModel);

                            if (newEndPoint.IsEqual(startPoint, 1e-6)) {
                                delete m_editLine;
                                m_editLine = nullptr;
                                return;
                            }

                            // ③ 更新 label 文本：直接显示用户输入值（避免后面再计算覆盖）
                            {
                                QString text = QString("Distance: %1 mm").arg(distanceMm, 0, 'f', 3);
                                m_label->SetText(TCollection_ExtendedString(text.toStdWString().c_str()));
                                m_context->Redisplay(m_label, Standard_False);
                            }

                            // ④ 更新 manipulator 位置（保持姿态，只改位置）
                            gp_Pnt endPoint = curAx2.Location();
                            float mat[12] = {};
                            occAx2ToMat(curAx2, mat, 1);
                            mat[3] = static_cast<float>(newEndPoint.X());
                            mat[7] = static_cast<float>(newEndPoint.Y());
                            mat[11] = static_cast<float>(newEndPoint.Z());
                            gp_Ax2 newEndAx2;
                            occMatToAx2(mat, newEndAx2, 1);
                            m_aManipulator->SetPosition(newEndAx2);

                            // ⑤ 更新所有被操纵对象：沿 endPoint → newEndPoint 的位移
                            gp_Vec translationVector(endPoint, newEndPoint);
                            gp_Trsf transformation;
                            transformation.SetTranslation(translationVector);

                            Handle(AIS_ManipulatorObjectSequence) objects = m_aManipulator->Objects();
                            AIS_ManipulatorObjectSequence::Iterator anObjIter(*objects);
                            for (; anObjIter.More(); anObjIter.Next()) {
                                const Handle(AIS_InteractiveObject)& anObj = anObjIter.ChangeValue();
                                gp_Trsf oldTransformation = anObj->Transformation();
                                const Handle(TopLoc_Datum3D)& aParentTrsf = anObj->CombinedParentTransformation();
                                if (!aParentTrsf.IsNull() && aParentTrsf->Form() != gp_Identity) {
                                    const gp_Trsf aNewLocalTrsf =
                                        aParentTrsf->Trsf().Inverted() * transformation * aParentTrsf->Trsf() * oldTransformation;
                                    anObj->SetLocalTransformation(aNewLocalTrsf);
                                }
                                else {
                                    anObj->SetLocalTransformation(transformation * oldTransformation);
                                }
                            }

                            // ⑥ 用“正确的轴向”调用 ShowTransformTrajectory
                            gp_Ax1 axis1(startPoint, axisDir);
                            ShowTransformTrajectory(m_context, axis1, startPoint, newEndPoint);

                            redrawView();

                            delete m_editLine;
                            m_editLine = nullptr;
                            }, Qt::UniqueConnection);




                        //connect(m_editLine, &QLineEdit::editingFinished, this, [this]() {
                        //    QString distanceText = m_editLine->text();
                        //    QString text = "Distance: " + distanceText + " mm";
                        //    //qInfo() << "distanceText:" << text;

                        //    if (!text.isEmpty() && !m_label.IsNull()&& m_editLine->hasFocus()) {
                        //        
                        //        // 移动
                        //        gp_Pnt midPoint = m_label->Position();
                        //        gp_Ax2 tmpEndAx2 = m_aManipulator->Position();
                        //        gp_Pnt endPoint = tmpEndAx2.Location();
                        //        Standard_Real midDistance = midPoint.Distance(endPoint);

                        //        gp_Vec vec(midPoint, endPoint);
                        //        gp_Dir dir(vec);
                        //        gp_Dir revDir = dir.Reversed();
                        //        gp_Pnt startPoint = midPoint.Translated(gp_Vec(revDir) * midDistance);
                        //        // 恢复初始点
                        //        m_initialPosition = startPoint;
                        //        //qInfo() << "distanceText:" << distanceText.toDouble();
                        //        double distanceDouble = distanceText.toDouble();
                        //        gp_Pnt newEndPoint;
                        //        // 前一次是反向移动
                        //        if ((std::abs(endPoint.X() - startPoint.X()) > 1e-6 && endPoint.X() < startPoint.X()) || 
                        //            (std::abs(endPoint.Y() - startPoint.Y()) > 1e-6 && endPoint.Y() < startPoint.Y()) || 
                        //            (std::abs(endPoint.Z() - startPoint.Z()) > 1e-6 && endPoint.Z() < startPoint.Z())) {
                        //            newEndPoint = startPoint.Translated(gp_Vec(revDir) * (distanceText.toDouble()));
                        //        }
                        //        else {
                        //            newEndPoint = startPoint.Translated(gp_Vec(dir) * (distanceText.toDouble()));
                        //        }
                        //        //if (distanceDouble < 0) {
                        //            //newEndPoint = startPoint.Translated(gp_Vec(dir) * (distanceText.toDouble()));
                        //        //}
                        //        //else {
                        //            //newEndPoint = startPoint.Translated(gp_Vec(dir) * (distanceText.toDouble()));
                        //        //}
                        //        

                        //        // 新拖动的点距离太近约等于没有拉动这种操作不允许
                        //        if (newEndPoint.IsEqual(startPoint, 1e-6)) {
                        //            return;
                        //        }

                        //        m_label->SetText(TCollection_ExtendedString(text.toStdWString().c_str()));
                        //        m_context->Redisplay(m_label, true);


                        //        //Standard_Real newDistance = newEndPoint.Distance(startPoint);
                        //        float mat[12] = {};
                        //        occAx2ToMat(tmpEndAx2, mat, 1);
                        //        mat[3] = newEndPoint.X();
                        //        mat[7] = newEndPoint.Y();
                        //        mat[11] = newEndPoint.Z();
                        //        gp_Ax2 newEndAx2;
                        //        occMatToAx2(mat, newEndAx2, 1);
                        //        m_aManipulator->SetPosition(newEndAx2);

                        //        //qInfo() << "handleMouseButtonRelease midPoint(X,Y,Z):" << midPoint.X() << " , " << midPoint.Y() << " , " << midPoint.Z() << " , ";
                        //        //qInfo() << "handleMouseButtonRelease endPoint(X,Y,Z):" << endPoint.X() << " , " << endPoint.Y() << " , " << endPoint.Z() << " , ";
                        //        //qInfo() << "handleMouseButtonRelease startPoint(X,Y,Z):" << startPoint.X() << " , " << startPoint.Y() << " , " << startPoint.Z() << " , ";
                        //        //qInfo() << "handleMouseButtonRelease newEndPoint(X,Y,Z):" << newEndPoint.X() << " , " << newEndPoint.Y() << " , " << newEndPoint.Z() << " , ";
                        //        //qInfo() << "handleMouseButtonRelease m_initialPosition(X,Y,Z):" << m_initialPosition.X() << " , " << m_initialPosition.Y() << " , " << m_initialPosition.Z() << " , ";

                        //        //qInfo() << "handleMouseButtonRelease m_lastOperation:" << m_lastOperation;
                        //        //qInfo() << "handleMouseButtonRelease m_meshId:" << m_meshId;
                        //        //qInfo() << "handleMouseButtonRelease m_aManipulator->IsAttached():" << m_aManipulator->IsAttached();

                        //        //Standard_Integer x1, y1;
                        //        //m_occView->v3dView()->Convert(m_initialPosition.X(), m_initialPosition.Y(), m_initialPosition.Z(), x1, y1);
                        //        ////qInfo() << "handleMouseButtonRelease V3d_View::Convert:" << x1 << " , " << y1;
                        //        //Standard_Integer x2, y2;
                        //        //m_occView->v3dView()->Convert(newEndPoint.X(), newEndPoint.Y(), newEndPoint.Z(), x2, y2);
                        //        //qInfo() << "handleMouseButtonRelease newEndPoint V3d_View::Convert:" << x2 << " , " << y2;

                        //        // 获取绑定物体的移动向量
                        //        gp_Vec translationVector(endPoint, newEndPoint);
                        //        gp_Trsf transformation;
                        //        transformation.SetTranslation(translationVector);

                        //        Handle(AIS_ManipulatorObjectSequence) objects = m_aManipulator->Objects();
                        //        AIS_ManipulatorObjectSequence::Iterator anObjIter(*objects);
                        //        for (; anObjIter.More(); anObjIter.Next())
                        //        {
                        //            const Handle(AIS_InteractiveObject)& anObj = anObjIter.ChangeValue();
                        //            gp_Trsf oldTransformation = anObj->Transformation();                                    
                        //            //anObj->SetLocalTransformation(transformation * oldTransformation);

                        //            const Handle(TopLoc_Datum3D)& aParentTrsf = anObj->CombinedParentTransformation();
                        //            if (!aParentTrsf.IsNull() && aParentTrsf->Form() != gp_Identity)
                        //            {
                        //                // recompute local transformation relative to parent transformation
                        //                const gp_Trsf aNewLocalTrsf =
                        //                    aParentTrsf->Trsf().Inverted() * transformation * aParentTrsf->Trsf() * oldTransformation;
                        //                anObj->SetLocalTransformation(aNewLocalTrsf);
                        //            }
                        //            else
                        //            {
                        //                anObj->SetLocalTransformation(transformation * oldTransformation);
                        //            }
                        //        }
                        //        

                        //        gp_Ax1 axis1;
                        //        ShowTransformTrajectory(m_context, axis1, m_initialPosition, newEndPoint);

                        //        redrawView();
                        //    }
                        //    
                        //    // 删除 QLineEdit 对象
                        //    delete m_editLine;
                        //    m_editLine = nullptr;  // 确保指针不再指向已删除的对象

                        //    }, Qt::UniqueConnection);
                        


                    }


                    return;
                }
                else if (selected == m_rolabel) {
                    QString currentText = QString::fromUtf16(m_rolabel->Text().ToExtString());

                    QWidget* parentWidget = m_occView->widget()->parentWidget();  // WidgetGuiDocument

                    if (!m_editLine) {
                        m_editLine = new QLineEdit(parentWidget); // 覆盖在 viewer 上
                        setMoveLine = true;
                        //m_editLine = new QLineEdit(nullptr);  // 没有父控件，系统浮动窗口
                        //m_editLine->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
                        m_editLine->setStyleSheet("background: white; color: black; border: 1px solid red;");
                        m_editLine->setAlignment(Qt::AlignCenter);
                        //QDoubleValidator* validator = new QDoubleValidator(-359, 360, 0, this);
                        //m_editLine->setValidator(validator);
                        m_editLine->setValidator(new QRegularExpressionValidator(QRegularExpression("^-?(360(\\.0+)?|([1-9]?\\d|[1-2]\\d{2}|3[0-5]\\d|359)(\\.\\d+)?|0(\\.\\d+)?)$")));
                        m_editLine->resize(80, 24);
                        m_editLine->setFrame(true);
                        m_editLine->hide();


                        // 设置文本框位置
                        //QStringList qlist = currentText.split(" ");
                        m_editLine->setText(currentText);

                        QPoint editPoint;
                        editPoint.setX(QCursor::pos().x());
                        editPoint.setY(QCursor::pos().y() - 50);
                        m_editLine->move(editPoint);
                        //qInfo() << "QCursor::pos():" << QCursor::pos();

                        m_editLine->show();       // 显示
                        m_editLine->raise();      // 放到最上层
                        m_editLine->setFocusPolicy(Qt::StrongFocus);
                        m_editLine->setFocus();   // 获得焦点

                        //qInfo() << "handleMouseButtonRelease create m_label!!!";

                        connect(m_editLine, &QLineEdit::editingFinished, this, [this]() {
                            QString distanceText = m_editLine->text();
                            //QString text = "Distance: " + distanceText + " mm";
                            //qInfo() << "distanceText:" << text;
                            if (!distanceText.isEmpty() && !m_rolabel.IsNull() && m_editLine->hasFocus()) {
                                Standard_Real angleNew = distanceText.toDouble() * M_PI / 180;

                                // 数值为0的时候相当于没有转动，这种操作不允许
                                if (std::abs(angleNew - tempAngle) <= 1e-6) {
                                    return;
                                }

                                m_rolabel->SetText(TCollection_ExtendedString(distanceText.toStdWString().c_str()));
                                m_context->Redisplay(m_rolabel, true);

                                // 获取当前位置作为旋转中心
                                gp_Pnt currentPoint = m_aManipulator->Position().Location();
                                // 获取旋转轴,tmpRotationAxis为保存的前续操作旋转方向
                                gp_Ax1 ax1(currentPoint, tmpRotationAxis.Direction());
                                // 获取当前位置和方向
                                gp_Ax2 currentPosition = m_aManipulator->Position();
                                gp_Trsf currentRotation = m_aManipulator->Transformation();

                                //currentRotation.

                                // 获取设置前的旋转弧度
                                gp_Quaternion deltaRotation = currentRotation.GetRotation() * m_initialRotation.GetRotation().Inverted();
                                gp_Vec currentVec;
                                Standard_Real currentAngle;
                                currentRotation.GetRotation().GetVectorAndAngle(currentVec, currentAngle);
                                /*qInfo() << "currentRotation: " << currentVec.X() << " , " << currentVec.Y() << " , " << currentVec.Z();
                                qInfo() << "currentAngle: " << currentAngle;

                                gp_Vec deltaVec;
                                Standard_Real deltaAngle;
                                deltaRotation.GetVectorAndAngle(deltaVec, deltaAngle);
                                qInfo() << "deltaRotation: " << deltaVec.X() << " , " << deltaVec.Y() << " , " << deltaVec.Z();
                                qInfo() << "deltaAngle: " << deltaAngle;

                                gp_Vec orignVec;
                                Standard_Real orignAngle;
                                m_initialRotation.GetRotation().GetVectorAndAngle(orignVec, orignAngle);
                                qInfo() << "orignRotation: " << orignVec.X() << " , " << orignVec.Y() << " , " << orignVec.Z();
                                qInfo() << "orignAngle: " << orignAngle;*/


                                gp_Quaternion currentRotationQuaternion = currentRotation.GetRotation();
                                gp_Quaternion newRotationQuaternion = currentRotationQuaternion;
                                newRotationQuaternion.SetVectorAndAngle(currentVec, angleNew);

                                gp_Trsf newTrsf;
                                newTrsf.SetRotation(newRotationQuaternion);


                                qInfo() << "recorde last end Angle: " << tempAngle;
                                

                                // 创建旋转变换
                                gp_Trsf rotation;
                                rotation.SetRotation(tmpRotationAxis, angleNew - tempAngle);

                                //m_aManipulator->SetPosition(ax2);
                                /*Handle(AIS_InteractiveObject) interactiveObj =
                                    Handle(AIS_InteractiveObject)::DownCast(m_aManipulator);
                                interactiveObj->SetLocalTransformation(newTrsf);*/

                                // 获取平移向量（原点位置）
                                gp_Pnt newPoint = currentPoint.Transformed(newTrsf);

                                // 获取旋转矩阵
                                gp_Mat rotationMat = newTrsf.HVectorialPart();

                                // 提取 Z 轴方向（旋转矩阵的第三列）
                                gp_Dir zDir(rotationMat(1, 3), rotationMat(2, 3), rotationMat(3, 3));

                                // 提取 X 轴方向（旋转矩阵的第一列）
                                gp_Dir xDir(rotationMat(1, 1), rotationMat(2, 1), rotationMat(3, 1));

                                // 确保 X 轴与 Z 轴正交
                                if (Abs(xDir.Dot(zDir)) > 1e-10) {
                                    // 如果不正交，计算正交的 X 轴（使用 Z 轴和任意向量叉乘）
                                    gp_Dir tempDir = (Abs(zDir.X()) < 0.9) ? gp_Dir(1, 0, 0) : gp_Dir(0, 1, 0);
                                    xDir = gp_Dir(zDir.Crossed(tempDir));
                                }

                                // 创建并返回 gp_Ax2
                                gp_Ax2 newEndAx2(currentPoint, zDir, xDir);


                                // 6. 更新 AIS_Manipulator 的位置
                                //m_aManipulator->SetPosition(gp_Ax2(newPoint, tmpRotationAxis.Direction()));
                                m_aManipulator->SetPosition(newEndAx2);


                                Handle(AIS_ManipulatorObjectSequence) objects = m_aManipulator->Objects();
                                AIS_ManipulatorObjectSequence::Iterator anObjIter(*objects);
                                for (; anObjIter.More(); anObjIter.Next())
                                {
                                    const Handle(AIS_InteractiveObject)& anObj = anObjIter.ChangeValue();
                                    gp_Trsf oldTransformation = anObj->Transformation();
                                    //anObj->SetLocalTransformation(rotation * oldTransformation);

                                    const Handle(TopLoc_Datum3D)& aParentTrsf = anObj->CombinedParentTransformation();
                                    if (!aParentTrsf.IsNull() && aParentTrsf->Form() != gp_Identity)
                                    {
                                        // recompute local transformation relative to parent transformation
                                        const gp_Trsf aNewLocalTrsf =
                                            aParentTrsf->Trsf().Inverted() * rotation * aParentTrsf->Trsf() * oldTransformation;
                                        anObj->SetLocalTransformation(aNewLocalTrsf);
                                    }
                                    else
                                    {
                                        anObj->SetLocalTransformation(rotation * oldTransformation);
                                    }
                                }


                                gp_Ax1 ax1New(newEndAx2.Location(), tmpRotationAxis.Direction());
                                ShowRotationTrajectory(m_context, ax1New, 0.0, angleNew);


                                redrawView();
                            }

                            // 删除 QLineEdit 对象
                            delete m_editLine;
                            m_editLine = nullptr;  // 确保指针不再指向已删除的对象

                            }, Qt::UniqueConnection);
                    }
                    return;
                }
            }
        }

        setMoveLine = false;

        if (m_aManipulatorReady && m_aManipulatorDo)
        {
            int tmpActiveAxisIndex = m_aManipulator->ActiveAxisIndex();
            //if (0 <= tmpActiveAxisIndex)
            {
                //emit CGlobalEventSender::getInstance()->occManipulatorMoved();

                m_aManipulator->StopTransform(Standard_True);	// 重置起始变换参数（函数参数为 Standard_False 则撤销本次的变换）

                m_aManipulator->SetPart(0, AIS_ManipulatorMode::AIS_MM_Translation, Standard_True); // 启用了 X 轴的平移
                m_aManipulator->SetPart(1, AIS_ManipulatorMode::AIS_MM_Translation, Standard_True); // 启用了 Y 轴的平移
                m_aManipulator->SetPart(2, AIS_ManipulatorMode::AIS_MM_Translation, Standard_True); // 启用了 Z 轴的平移
                m_aManipulator->SetPart(0, AIS_ManipulatorMode::AIS_MM_Rotation, Standard_True); // 启用了 X 轴的旋转
                m_aManipulator->SetPart(1, AIS_ManipulatorMode::AIS_MM_Rotation, Standard_True); // 启用了 Y 轴的旋转
                m_aManipulator->SetPart(2, AIS_ManipulatorMode::AIS_MM_Rotation, Standard_True); // 启用了 Z 轴的旋转

                gp_Ax2 tmpAx21 = m_aManipulator->Position();
                m_aManipulator->Attach(m_aSequence, m_attachOption);

                if (-1 != m_meshId)
                {
                    float tmpMat[12] = { 0 };
                    //simGetObjectMatrix_internal(m_meshId, -1, tmpMat);
                    gp_Ax2 tmpAx2;
                    occMatToAx2(tmpMat, tmpAx2);
                    m_aManipulator->SetPosition(tmpAx2);
                }
                else
                {
                    m_aManipulator->SetPosition(tmpAx21);
                }

                m_aManipulator->DeactivateCurrentMode();
                m_aManipulatorDo = false;
            }
                        
        }
    }

    void WidgetOccViewController::handleMouseWheel(const QWheelEvent* event)
    {
        const QPoint delta = event->angleDelta();
        if (delta.y() > 0 || (delta.y() == 0 && delta.x() > 0))
            this->zoomIn();
        else
            this->zoomOut();
    }

    class WidgetOccViewController::Mayo_ActionMatcher : public ActionMatcher {
    public:
        Mayo_ActionMatcher(const InputSequence* seq) : ActionMatcher(seq) {}

        bool matchRotation() const override {
            return this->inputs.equal({ Qt::LeftButton });
        }

        bool matchPan() const override {
            return this->inputs.equal({ Qt::RightButton });
        }

        bool matchZoom() const override {
            return this->inputs.equal({ Qt::LeftButton, Qt::RightButton })
                || this->inputs.equal({ Qt::RightButton, Qt::LeftButton });
        }

        bool matchWindowZoom() const override {
            return this->inputs.equal({ Qt::Key_Control, Qt::LeftButton });
        }
    };

    class WidgetOccViewController::Catia_ActionMatcher : public ActionMatcher {
    public:
        Catia_ActionMatcher(const InputSequence* seq) : ActionMatcher(seq) {
            m_timer.start();
        }

        bool matchRotation() const override {
            return this->inputs.equal({ Qt::MiddleButton, Qt::LeftButton })
                || this->inputs.equal({ Qt::MiddleButton, Qt::RightButton });
        }

        bool matchPan() const override {
            return this->inputs.equal({ Qt::MiddleButton }) && !this->matchZoom();
        }

        bool matchZoom() const override {
            return this->inputs.equal({ Qt::MiddleButton })
                && m_beforeLastOp == InputSequence::Operation::Push
                && (m_beforeLastInput == Qt::LeftButton || m_beforeLastInput == Qt::RightButton)
                && this->inputs.lastOperation() == InputSequence::Operation::Release
                && this->inputs.lastInput() == m_beforeLastInput
                && (m_lastTimestamp_ms - m_beforeLastTimestamp_ms) < 750
                ;
        }

        bool matchWindowZoom() const override {
            return this->inputs.equal({ Qt::Key_Control, Qt::MiddleButton });
        }

        void onInputPrePush(Input /*in*/) override {
            this->recordBeforeLastOperation();
        }

        void onInputPreRelease(Input /*in*/) override {
            this->recordBeforeLastOperation();
        }

        void onInputCleared() override {
            m_beforeLastOp = InputSequence::Operation::None;
            m_beforeLastInput = -1;
            m_beforeLastTimestamp_ms = 0;
            m_lastTimestamp_ms = 0;
            m_timer.restart();
        }

    private:
        void recordBeforeLastOperation() {
            m_beforeLastOp = this->inputs.lastOperation();
            m_beforeLastInput = this->inputs.lastInput();
            m_beforeLastTimestamp_ms = m_lastTimestamp_ms;
            m_lastTimestamp_ms = m_timer.elapsed();
        }

        InputSequence::Operation m_beforeLastOp = InputSequence::Operation::None;
        Input m_beforeLastInput = -1;
        int64_t m_beforeLastTimestamp_ms = 0;
        int64_t m_lastTimestamp_ms = 0;
        QElapsedTimer m_timer;
    };

    class WidgetOccViewController::SolidWorks_ActionMatcher : public ActionMatcher {
    public:
        SolidWorks_ActionMatcher(const InputSequence* seq) : ActionMatcher(seq) {}

        bool matchRotation() const override {
            return this->inputs.equal({ Qt::MiddleButton });
        }

        bool matchPan() const override {
            return this->inputs.equal({ Qt::Key_Control, Qt::MiddleButton });
        }

        bool matchZoom() const override {
            return this->inputs.equal({ Qt::Key_Shift, Qt::MiddleButton });;
        }

        bool matchWindowZoom() const override {
            return this->inputs.equal({ Qt::Key_Control, Qt::LeftButton });
        }
    };

    class WidgetOccViewController::Unigraphics_ActionMatcher : public ActionMatcher {
    public:
        Unigraphics_ActionMatcher(const InputSequence* seq) : ActionMatcher(seq) {}

        bool matchRotation() const override {
            return this->inputs.equal({ Qt::MiddleButton });
        }

        bool matchPan() const override {
            return this->inputs.equal({ Qt::MiddleButton, Qt::RightButton });
        }

        bool matchZoom() const override {
            return this->inputs.equal({ Qt::MiddleButton, Qt::LeftButton });;
        }

        bool matchWindowZoom() const override {
            return this->inputs.equal({ Qt::Key_Control, Qt::LeftButton });
        }
    };

    class WidgetOccViewController::ProEngineer_ActionMatcher : public ActionMatcher {
    public:
        ProEngineer_ActionMatcher(const InputSequence* seq) : ActionMatcher(seq) {}

        bool matchRotation() const override {
            return this->inputs.equal({ Qt::MiddleButton });
        }

        bool matchPan() const override {
            return this->inputs.equal({ Qt::Key_Shift, Qt::MiddleButton });
        }

        bool matchZoom() const override {
            return this->inputs.equal({ Qt::Key_Control, Qt::MiddleButton });;
        }

        bool matchWindowZoom() const override {
            return this->inputs.equal({ Qt::Key_Control, Qt::LeftButton });
        }
    };

    std::unique_ptr<WidgetOccViewController::ActionMatcher>
        WidgetOccViewController::createActionMatcher(View3dNavigationStyle style, const InputSequence* seq)
    {
        switch (style) {
        case View3dNavigationStyle::Mayo: return std::make_unique<Mayo_ActionMatcher>(seq);
        case View3dNavigationStyle::Catia: return std::make_unique<Catia_ActionMatcher>(seq);
        case View3dNavigationStyle::SolidWorks: return std::make_unique<SolidWorks_ActionMatcher>(seq);
        case View3dNavigationStyle::Unigraphics: return std::make_unique<Unigraphics_ActionMatcher>(seq);
        case View3dNavigationStyle::ProEngineer: return std::make_unique<ProEngineer_ActionMatcher>(seq);
        }
        return {};
    }

    //void Mayo::WidgetOccViewController::dragStartCallback(void* data, SoDragger* d)
    //{
    //}
    //
    //void Mayo::WidgetOccViewController::dragFinishCallback(void* data, SoDragger* d)
    //{
    //}
    //
    //void Mayo::WidgetOccViewController::dragMotionCallback(void* data, SoDragger* d)
    //{
    //}

    void WidgetOccViewController::InputSequence::push(Input in)
    {
        auto itFound = std::find(m_inputs.cbegin(), m_inputs.cend(), in);
        if (itFound != m_inputs.cend())
            m_inputs.erase(itFound);

        if (m_fnPrePushCallback)
            m_fnPrePushCallback(in);

        m_inputs.push_back(in);
        m_lastOperation = Operation::Push;
        m_lastInput = in;
    }

    void WidgetOccViewController::InputSequence::release(Input in)
    {
        auto itRemoved = std::remove(m_inputs.begin(), m_inputs.end(), in);
        if (itRemoved != m_inputs.end()) {
            if (m_fnPreReleaseCallback)
                m_fnPreReleaseCallback(in);

            m_inputs.erase(itRemoved, m_inputs.end());
            m_lastOperation = Operation::Release;
            m_lastInput = in;
        }
    }

    void WidgetOccViewController::InputSequence::clear()
    {
        m_inputs.clear();
        m_lastOperation = Operation::None;
        m_lastInput = -1;
        if (m_fnClearCallback)
            m_fnClearCallback();
    }

    bool WidgetOccViewController::InputSequence::equal(std::initializer_list<Input> other) const
    {
        return std::equal(m_inputs.cbegin(), m_inputs.cend(), other.begin(), other.end());
    }

    void WidgetOccViewController::printWidgetTree(QWidget* widget, int depth) {
        if (!widget)
            return;

        qInfo().noquote() << depth << "-"
            << widget->metaObject()->className()
            << (widget->objectName().isEmpty() ? "" : "[" + widget->objectName() + "]")
            << (widget->isVisible() ? "[visible]" : "[hidden]")
            << QString("size=(%1x%2)").arg(widget->width()).arg(widget->height());

        const auto children = widget->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget* child : children) {
            printWidgetTree(child, depth + 1);
        }
    }
} // namespace Mayo
