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

#include <PrsDim_AngleDimension.hxx>
#include <Prs3d_DimensionAspect.hxx>


//#include "SoFCCSysDragger.h"
//using namespace Gui;

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
            {
            }
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
        // 
        m_aManipulator->SetModeActivationOnDetection(Standard_False);
        //m_aManipulator->DeactivateCurrentMode();
        m_aManipulator->Detach();

        m_meshId = -1;
    }

    gp_Pnt Mayo::WidgetOccViewController::getTransform()
    {
        return m_posTransform;
    }

    void Mayo::WidgetOccViewController::ShowRotationTrajectory(const Handle(AIS_InteractiveContext)& ctx,
        const gp_Ax1& rotationAxis,
        double startAngle,
        double endAngle)
    {
        if (!m_trajectoryShape.IsNull()) {
            ctx->Remove(m_trajectoryShape, Standard_False); // 移除旧轨迹
            m_trajectoryShape.Nullify();
        }

        // 清掉平移的距离标注（辅助直线 + 文字），保证与轨迹同步消失
        if (!m_translateDim.IsNull()) {
            ctx->Remove(m_translateDim, Standard_False);
            m_translateDim.Nullify();
        }

        // 【新增】清理 OCC 旋转角度标注与两条线
        if (!m_rotLineBefore.IsNull()) {
            ctx->Remove(m_rotLineBefore, Standard_False);
            m_rotLineBefore.Nullify();
        }
        if (!m_rotLineAfter.IsNull()) {
            ctx->Remove(m_rotLineAfter, Standard_False);
            m_rotLineAfter.Nullify();
        }
        if (!m_rotAngleDim.IsNull()) {
            ctx->Remove(m_rotAngleDim, Standard_False);
            m_rotAngleDim.Nullify();
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

        // 【新增】清理 OCC 角度标注版本的两条线与角度标注
        if (!m_rotLineBefore.IsNull()) {
            ctx->Remove(m_rotLineBefore, Standard_False);
            m_rotLineBefore.Nullify();
        }
        if (!m_rotLineAfter.IsNull()) {
            ctx->Remove(m_rotLineAfter, Standard_False);
            m_rotLineAfter.Nullify();
        }
        if (!m_rotAngleDim.IsNull()) {
            ctx->Remove(m_rotAngleDim, Standard_False);
            m_rotAngleDim.Nullify();
        }


        if (std::abs(endAngle - startAngle) <= 1e-6) {
            ctx->UpdateCurrentViewer(); // 确保 Remove 立即刷新显示
            return;
        }


        tempAngle = endAngle;

        // ===== 新增：旋转轨迹/文字颜色跟随当前旋转轴（X/Y/Z） =====
        // 通过 rotationAxis.Direction() 与操纵器当前 Ax2 的 X/Y/Z 方向做 dot，取最接近的轴作为颜色来源
        Quantity_Color trajColor(Quantity_NOC_BLACK);
        if (!m_aManipulator.IsNull()) {
            const gp_Ax2 ax2 = m_aManipulator->Position();
            const gp_Dir d = rotationAxis.Direction();

            const double dx = std::abs(d.Dot(ax2.XDirection()));
            const double dy = std::abs(d.Dot(ax2.YDirection()));
            const double dz = std::abs(d.Dot(ax2.Direction())); // Z

            int axisIndexForColor = (dx >= dy && dx >= dz) ? 0 : (dy >= dz ? 1 : 2);
            trajColor = colorFromAxisIndex(axisIndexForColor); // 0->红,1->绿,2->蓝
        }
        // ==========================================================


// ==========================================================
// 新方案：两条线（旋转前/旋转后） + OCC 角度标注（PrsDim_AngleDimension）
// ==========================================================

        const gp_Pnt center = rotationAxis.Location();
        const gp_Dir axisDir = rotationAxis.Direction();

        // 线段长度：沿用你之前按相机距离取的尺度（防止太短看不见）

        Standard_Real viewDist = 200.0;
        Standard_Real lineLen = 0.4 * center.Distance(m_occView->v3dView().get()->Camera()->Eye());
        lineLen = std::max<Standard_Real>(0.35 * viewDist, 80.0); // 给个下限，避免相机很近时太短

        // 选一个与旋转轴垂直的参考方向 refVec（保证两条线在同一平面内，角度标注稳定）
        gp_Vec refVec = gp_Vec(axisDir).Crossed(gp_Vec(0, 0, 1));
        if (refVec.SquareMagnitude() < 1e-12) {
            refVec = gp_Vec(axisDir).Crossed(gp_Vec(1, 0, 0));
        }
        refVec.Normalize();

        // v0 是“未旋转”的参考向量
        gp_Vec v0 = refVec * lineLen;

        // 旋转前/后的向量（围绕 rotationAxis 旋转）
        gp_Vec vBefore = v0;
        vBefore.Rotate(rotationAxis, startAngle);

        gp_Vec vAfter = v0;
        vAfter.Rotate(rotationAxis, endAngle);

        // 两条线的端点
        const gp_Pnt pBefore = center.Translated(vBefore);
        const gp_Pnt pAfter = center.Translated(vAfter);

        // 做两条边（共享同一个顶点 center）
        TopoDS_Edge edgeBefore = BRepBuilderAPI_MakeEdge(center, pBefore);
        TopoDS_Edge edgeAfter = BRepBuilderAPI_MakeEdge(center, pAfter);

        // 旋转前的线（建议黑色/灰色）
        m_rotLineBefore = new AIS_Shape(edgeBefore);
        m_rotLineBefore->SetColor(Quantity_NOC_BLACK);
        m_rotLineBefore->SetWidth(2.0);
        m_rotLineBefore->SetZLayer(Graphic3d_ZLayerId_Topmost);
        ctx->SetDisplayPriority(m_rotLineBefore, 10);
        ctx->Display(m_rotLineBefore, Standard_False);

        // 旋转后的线（建议用轴颜色 trajColor，和你当前逻辑一致）
        m_rotLineAfter = new AIS_Shape(edgeAfter);
        m_rotLineAfter->SetColor(trajColor);
        m_rotLineAfter->SetWidth(2.0);
        m_rotLineAfter->SetZLayer(Graphic3d_ZLayerId_Topmost);
        ctx->SetDisplayPriority(m_rotLineAfter, 11);
        ctx->Display(m_rotLineAfter, Standard_False);

        // 角度标注（类似你示例：PrsDim_AngleDimension(edge1, edge2)）
        m_rotAngleDim = new PrsDim_AngleDimension(edgeBefore, edgeAfter);

        Handle(Prs3d_DimensionAspect) dimensionAspect = new Prs3d_DimensionAspect();
        dimensionAspect->MakeArrows3d(Standard_False);
        dimensionAspect->MakeText3d(Standard_False);          // false：用 2D 文本显示
        dimensionAspect->TextAspect()->SetHeight(20.0);
        dimensionAspect->MakeTextShaded(true);
        
        // 颜色：圆弧 + 文字 跟随旋转轴颜色（与 m_rotLineAfter 一致）
        dimensionAspect->SetCommonColor(trajColor);

        dimensionAspect->MakeUnitsDisplayed(false);

        m_rotAngleDim->SetDisplayUnits("deg");
        m_rotAngleDim->SetDimensionAspect(dimensionAspect);

        // -----------------------------
        // 1) 让“标注圆弧”比默认更大一点（Flyout 越大，圆弧半径越大）
        // -----------------------------
        const Standard_Real flyout = lineLen * 1.8;   // 你现在没设flyout；0.80~0.95 都可微调
        m_rotAngleDim->SetFlyout(flyout);


        // -----------------------------
        // 2) 让数字在圆弧“里面”（圆弧在数字外侧）
        //    做法：把文字放到角平分线方向，并且半径 < flyout
        // -----------------------------
        auto normToPi = [](double a) {
            while (a > M_PI) a -= 2.0 * M_PI;
            while (a < -M_PI) a += 2.0 * M_PI;
            return a;
        };


        // 角平分线方向（midAngle）
        const double delta = normToPi(endAngle - startAngle);
        const double midAngle = startAngle + 0.5 * delta;

        gp_Vec vMid = v0;
        vMid.Rotate(rotationAxis, midAngle);
        if (vMid.SquareMagnitude() > 1e-12) {
            vMid.Normalize();
        }

        // textRadius 要小于 flyout：这样圆弧在数字外面
        const Standard_Real textRadius = flyout * 0.72;   // 0.65~0.80 可调：越小越靠内
        gp_Pnt textPos = center.Translated(vMid * textRadius);


        double signedAngleRad = normToPi(endAngle - startAngle);
        // 避免 -0.00
        if (std::abs(signedAngleRad) < 1e-10) signedAngleRad = 0.0;

        // SetCustomValue(Real) 以“模型单位”存储，显示时仍会按 SetDisplayUnits("deg") 做单位转换
        m_rotAngleDim->SetCustomValue(signedAngleRad);  // 负值会显示为负角度 :contentReference[oaicite:2]{index=2}

        m_rotAngleDim->SetTextPosition(textPos);

        m_rotAngleDim->SetZLayer(Graphic3d_ZLayerId_Topmost);
        ctx->SetDisplayPriority(m_rotAngleDim, 12);
        ctx->Display(m_rotAngleDim, Standard_False);

        ctx->UpdateCurrentViewer();

    }

    Quantity_Color Mayo::WidgetOccViewController::colorFromAxisIndex(int axisIndex)
    {
        switch (axisIndex) {
        case 0:
            return Quantity_Color(Quantity_NOC_RED);
        case 1:
            return Quantity_Color(Quantity_NOC_GREEN);
        case 2:
            return Quantity_Color(Quantity_NOC_BLUE);
        default:
            return Quantity_Color(Quantity_NOC_BLACK);
        }
    }


    void Mayo::WidgetOccViewController::ShowTransformTrajectory(const Handle(AIS_InteractiveContext)& ctx, const gp_Ax1& rotationAxis, gp_Pnt startPoint, gp_Pnt endPoint)
    {
        // 在更新轨迹的函数中：
        if (!m_trajectoryShape.IsNull()) {
            ctx->Remove(m_trajectoryShape, Standard_False); // 移除旧轨迹
            m_trajectoryShape.Nullify();
        }

        //if (!m_label.IsNull()) {
        //    ctx->Remove(m_label, Standard_False); // 移除旧轨迹的文字
        //    m_label.Nullify();
        //}

        if (!m_translateDim.IsNull()) {
            ctx->Remove(m_translateDim, Standard_False); // 移除旧轨迹的文字
            m_translateDim.Nullify();
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
            ctx->UpdateCurrentViewer(); // 确保 Remove 立即刷新显示
            return;
        }
        TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(startPoint, endPoint);
        Handle(AIS_Shape) lineShape = new AIS_Shape(edge);

        m_trajectoryShape = lineShape;


        // 优先使用“已记录的平移轴”（拖拽时你在外面已经写过 m_distanceAxisIndex = tmpActiveAxisIndex;）
        int axisIndexForColor = m_distanceAxisIndex;

        // 次选：如果你启用了绝对 anchor 冻结，也可以用冻结轴
        if (axisIndexForColor < 0 || axisIndexForColor > 2) {
            if (m_hasTranslateAbsAnchor) {
                axisIndexForColor = m_translateAbsAxisIndex;
            }
        }

        // 兜底：最后才去读 ActiveAxisIndex（注意：输入框场景下它经常是 -1）
        if (axisIndexForColor < 0 || axisIndexForColor > 2) {
            axisIndexForColor = (!m_aManipulator.IsNull()) ? m_aManipulator->ActiveAxisIndex() : -1;
        }

        const Quantity_Color trajColor = colorFromAxisIndex(axisIndexForColor);
        m_trajectoryShape->SetColor(trajColor);





        m_trajectoryShape->SetWidth(3);

        // 关键：放到 Topmost 图层 + 提升显示优先级
        m_trajectoryShape->SetZLayer(Graphic3d_ZLayerId_Topmost);
        ctx->SetDisplayPriority(m_trajectoryShape, 10);

        ctx->Display(m_trajectoryShape, Standard_False);

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

        //      oss << std::setprecision(3) << signedDistance;
        //      std::string distanceStr = oss.str();

        //      m_label = new AIS_TextLabel();
        //      m_label->SetText(TCollection_ExtendedString(distanceStr.c_str()));
        //      m_label->SetColor(trajColor);

        //      gp_Pnt midPoint((startPoint.XYZ() + endPoint.XYZ()) / 2.0);

        //      m_label->SetPosition(midPoint);

              //// ? Display ?? ZLayer??
        //      m_label->SetZLayer(Graphic3d_ZLayerId_Topmost);

        //      ctx->Display(m_label, Standard_False);

        // =======================12.26 =======================
        // ======= Simple dimension label (parallel dimension line next to trajectory) =======
        // Keep a cache value (optional, can be used later for edit-box prefill)
        m_translateDimValueMm = signedDistance;

        // Use a simple default plane (same as your reference snippet)
        //gp_Pln pln;
        //m_translateDim = new PrsDim_LengthDimension(edge, pln);

        Handle(Prs3d_DimensionAspect) dimensionAspect = new Prs3d_DimensionAspect();
        dimensionAspect->MakeArrows3d(true);   // 关键：确保箭头/标注线可见
        dimensionAspect->MakeText3d(true);     // 关键：确保文字按 3D 方式显示
        dimensionAspect->TextAspect()->SetHeight(20);
        dimensionAspect->MakeTextShaded(true);
        dimensionAspect->SetCommonColor(trajColor);
        dimensionAspect->MakeUnitsDisplayed(true);


        // 1) edgeDir：轨迹方向
        gp_Vec evec(startPoint, endPoint);
        if (evec.Magnitude() < 1e-9) return;
        gp_Dir edgeDir(evec);

        // 2) viewDir：相机视线方向（没有相机就用 (0,0,1)）
        gp_Dir viewDir(0, 0, 1);
        if (m_occView && m_occView->v3dView().get() && m_occView->v3dView().get()->Camera()) {
            auto cam = m_occView->v3dView().get()->Camera();
            gp_Vec vd(cam->Direction().X(), cam->Direction().Y(), cam->Direction().Z());
            if (vd.Magnitude() > 1e-9) viewDir = gp_Dir(vd);
        }

        // 3) offsetDir：在屏幕平面内、且垂直于 edge 的方向（用于把标注线“挪到旁边”）
        gp_Vec off = gp_Vec(viewDir).Crossed(gp_Vec(edgeDir));
        if (off.Magnitude() < 1e-9) {
            off = gp_Vec(0, 0, 1).Crossed(gp_Vec(edgeDir));
            if (off.Magnitude() < 1e-9) off = gp_Vec(0, 1, 0).Crossed(gp_Vec(edgeDir));
        }
        gp_Dir offsetDir(off);

        // 4) 用 edgeDir + offsetDir 构造 plane 的法向（保证 plane 含 edge 且能定义 flyout 方向）
        gp_Vec n = gp_Vec(edgeDir).Crossed(gp_Vec(offsetDir));
        if (n.Magnitude() < 1e-9) return;
        gp_Dir normalDir(n);

        // 5) plane：通过 midPoint，Z=normal，X=edgeDir（这样 Y 会自动成为“偏移方向”）
        gp_Pnt midPoint((startPoint.XYZ() + endPoint.XYZ()) / 2.0);
        gp_Ax3 ax3(midPoint, normalDir, edgeDir);
        gp_Pln pln(ax3);

        // 6) 创建维度标注 + 强制 flyout（生成平行标注线）
        m_translateDim = new PrsDim_LengthDimension(edge, pln);

        Handle(Prs3d_DimensionAspect) asp = new Prs3d_DimensionAspect();
        asp->MakeArrows3d(true);
        asp->MakeText3d(true);
        asp->MakeUnitsDisplayed(true);
        asp->MakeTextShaded(true);
        asp->TextAspect()->SetHeight(20);
        asp->SetCommonColor(Quantity_NOC_BLACK); // 先固定红色，确保你能看见
        m_translateDim->SetDimensionAspect(asp);
        m_translateDim->SetModelUnits("mm");
        m_translateDim->SetDisplayUnits("mm");

        // flyout 给大一点，确保明显分离（你现在“看不到第二条线”，就先写死）
        m_translateDim->SetFlyout(80.0);

        m_translateDim->SetZLayer(Graphic3d_ZLayerId_Topmost);
        ctx->Display(m_translateDim, Standard_True);
        ctx->UpdateCurrentViewer();



        //m_translateDim->SetModelUnits("mm");
        //m_translateDim->SetDisplayUnits("mm");
        //m_translateDim->SetDimensionAspect(dimensionAspect);

        //// Force a visible offset from the trajectory line (creates the parallel dimension line)
        //const Standard_Real edgeLen = startPoint.Distance(endPoint);
        //const Standard_Real flyout = std::max<Standard_Real>(20.0, std::min<Standard_Real>(edgeLen * 0.20, 120.0));
        ////m_translateDim->SetFlyout(80.0);

        //m_translateDim->SetZLayer(Graphic3d_ZLayerId_Topmost);
        //ctx->SetDisplayPriority(m_translateDim, 12);
        //ctx->Display(m_translateDim, Standard_True); // 关键：立即计算并显示
        //ctx->UpdateCurrentViewer();                  // 关键：强制刷新


        // NOTE: translation dimension is drawn by PrsDim_LengthDimension (dimension line + arrows + text)


        // ==============================================

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

        const QPoint currPos = m_occView->widget()->mapFromGlobal(event->globalPos());
        m_prevPos = toPosition(currPos);

        if (m_aManipulatorReady)
        {
            int currentOperation = -1;
            m_aManipulatorDo = true;
            m_aManipulator->SetModeActivationOnDetection(Standard_True);

            if (m_aManipulatorDo && m_aManipulator->HasActiveMode())
            {
                int tmpActiveAxisIndex = m_aManipulator->ActiveAxisIndex();
                int tmpActiveAxisMode = m_aManipulator->ActiveMode();

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



                if (!m_aManipulator.IsNull()
                    && AIS_MM_Translation == tmpActiveAxisMode
                    && tmpActiveAxisIndex >= 0 && tmpActiveAxisIndex <= 2
                    && m_aManipulator->HasActiveMode())  
                {
                    const gp_Ax2 ax2 = m_aManipulator->Position();

                    m_initialPosition = ax2.Location();

                    gp_Dir axisDir =
                        (tmpActiveAxisIndex == 0) ? ax2.XDirection() :
                        (tmpActiveAxisIndex == 1) ? ax2.YDirection() :
                        ax2.Direction();


                    if (!m_hasTranslateAbsAnchor || tmpActiveAxisIndex != m_translateAbsAxisIndex) {
                        m_translateAbsAnchorWorld = ax2.Location();   
                        m_translateAbsAxisWorld = axisDir;
                        m_translateAbsAxisIndex = tmpActiveAxisIndex;
                        m_hasTranslateAbsAnchor = true;
                    }
                }
                // ======================================================================



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

                                    if ((event->buttons() & Qt::LeftButton) != 0
                                        && AIS_MM_Translation == tmpActiveAxisMode
                                        && tmpActiveAxisIndex >= 0 && tmpActiveAxisIndex <= 2)
                                    {
                                 
                                        if (!m_hasTranslateAbsAnchor || tmpActiveAxisIndex != m_translateAbsAxisIndex) {

                                            m_translateAbsAnchorWorld = m_initialPosition;

                                            m_translateAbsAxisWorld = axisDir;
                                            m_translateAbsAxisIndex = tmpActiveAxisIndex;
                                            m_hasTranslateAbsAnchor = true;
                                        }
                                    }
                                    // =========================================================================

                                    m_distanceAxisIndex = tmpActiveAxisIndex;

                                    gp_Pnt startPoint = m_hasTranslateAbsAnchor ? m_translateAbsAnchorWorld : m_initialPosition;
                                    gp_Dir drawAxisDir = m_hasTranslateAbsAnchor ? m_translateAbsAxisWorld : axisDir;

                                    // 将当前点投影到“起点 + 轴向”的直线上，保证轨迹始终沿轴显示
                                    const gp_Vec vStartToCur(startPoint, currentPosition);
                                    const Standard_Real t = vStartToCur.Dot(gp_Vec(drawAxisDir)); // 轴向标量距离（可正可负）
                                    const gp_Pnt endOnAxis = startPoint.Translated(gp_Vec(drawAxisDir) * t);

                                    const gp_Ax1 axis1(startPoint, drawAxisDir);
                                    ShowTransformTrajectory(m_context, axis1, startPoint, currentPosition);

                                }
                            }
                            else if (AIS_MM_Rotation == tmpActiveAxisMode)
                            {
                                gp_Pnt currentPosition = m_aManipulator->Position().Location();

                                gp_Trsf currentRotation = m_aManipulator->Transformation();


                                gp_Quaternion deltaRotation = currentRotation.GetRotation() * m_initialRotation.GetRotation().Inverted();
                                //if (deltaRotation.W() < 0) deltaRotation = -deltaRotation; // 统一为 w > 0 的表示
                                static Standard_Real lastAngle = 0.0;
                                gp_Vec axis;
                                Standard_Real angle;
                                deltaRotation.GetVectorAndAngle(axis, angle);

                                qInfo() << "axis: " << axis.X() << " , " << axis.Y() << " , " << axis.Z();
                                qInfo() << "angle: " << angle;
                                qInfo() << "deltaRotation.W(): " << deltaRotation.W();


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

                                m_posTransform.SetX(axis.X());
                                m_posTransform.SetY(axis.Y());
                                m_posTransform.SetZ(angle * 180.0 / M_PI);

                                // 1) 当前操纵器选中的旋转轴
                                gp_Vec axisRotation;

                                // 2) deltaRotation 轴角分解得到的 axis / angle
                                gp_Vec deltaAxis = axis;
                                if (deltaAxis.Magnitude() > 1e-12) {
                                    deltaAxis.Normalize();
                                }


                                const gp_Ax2 ax2 = m_aManipulator->Position();

                                if (tmpActiveAxisIndex == 0)      axisRotation = ax2.XDirection();
                                else if (tmpActiveAxisIndex == 1) axisRotation = ax2.YDirection();
                                else                               axisRotation = ax2.Direction(); // Z

                                axisRotation.Normalize();

                                if (deltaRotation.W() < 0.0) deltaRotation = -deltaRotation;

                                gp_Vec qAxis;
                                Standard_Real qAngle = 0.0;
                                deltaRotation.GetVectorAndAngle(qAxis, qAngle);

                                double signedAngle = (qAxis.Dot(axisRotation) < 0.0) ? -qAngle : qAngle;

                                if (!m_hasRotateAbsAnchor || m_rotateAbsAxisIndex != tmpActiveAxisIndex) {
                                    m_rotateAbsAnchorWorld = m_aManipulator->Position().Location();
                                    m_rotateAbsAxisWorld = gp_Dir(axisRotation);
                                    m_rotateAbsAxisIndex = tmpActiveAxisIndex;
                                    m_hasRotateAbsAnchor = true;
                                    m_rotateAbsAngleRad = 0.0; 
                                }

                                m_rotateAbsAngleRad = signedAngle;

                                const gp_Ax1 drawAxis(m_rotateAbsAnchorWorld, m_rotateAbsAxisWorld);
                                ShowRotationTrajectory(m_context, drawAxis, 0.0, signedAngle);




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

        if (m_context && (!m_translateDim.IsNull() || !m_rolabel.IsNull())) {


            m_context->InitSelected();
            if (m_context->MoreSelected()) {
                const Handle(AIS_InteractiveObject)& selected = m_context->SelectedInteractive();


               // 处理距离文本（平移）输入框
                if (selected == m_translateDim) {
                    // 例如 "12.345 mm"
                    //QString currentText = QString::fromUtf16(m_label->Text().ToExtString());

                    const double oldDistanceMm = m_translateDimValueMm;
                    const QString numberPart = QString::number(oldDistanceMm, 'f', 3);

                    QWidget* parentWidget = m_occView->widget()->parentWidget();  // 通常是 WidgetGuiDocument

                    if (!m_editLine) {
                        m_editLine = new QLineEdit(parentWidget); // 覆盖在 viewer 上


                        m_editLine->setStyleSheet("background: white; color: black; border: 1px solid red;");
                        m_editLine->setAlignment(Qt::AlignCenter);
                        
                        m_editLine->setValidator(new QRegularExpressionValidator(
                            QRegularExpression("^-?(0|([1-9][0-9]*))(\\.[0-9]+)?$"),
                            m_editLine
                        ));
                        m_editLine->resize(150, 24);
                        m_editLine->setFrame(true);
                        m_editLine->hide();

                        // 输入框显示当前的总位移 a
                        m_editLine->setText(numberPart);

                        // ---------- 2. 把 m_label 的 3D 位置投影到屏幕坐标 ----------
                        Standard_Integer vx = 0, vy = 0;


                        gp_Pnt labelPnt = m_aManipulator->Position().Location();
                        if (m_hasTranslateDimTextPosWorld) {
                            labelPnt = m_translateDimTextPosWorld;
                        }

                        // OCCT 投影：世界坐标 -> 视口像素坐标
                        m_occView->v3dView()->Convert(labelPnt.X(), labelPnt.Y(), labelPnt.Z(), vx, vy);

                        // Convert 给的是 OCC 视图坐标（通常原点在左上/左下取决于实现）
                        // Mayo/OCCT 里一般可以直接当作 Qt widget 的局部坐标使用；
                        // 若你发现 Y 方向上下颠倒，再做一次 vy = viewHeight - vy

                        QWidget* viewWidget = m_occView->widget();
                        QWidget* parentWidget = viewWidget->parentWidget();

                        // 视图局部坐标 -> 全局 -> parentWidget 局部
                        QPoint viewLocalPos(vx, vy);
                        QPoint globalPos = viewWidget->mapToGlobal(viewLocalPos);
                        QPoint localPos = parentWidget->mapFromGlobal(globalPos);

                        // 让输入框居中贴近文字（微调偏移可按需要改）
                        localPos -= QPoint(m_editLine->width() / 2, m_editLine->height() / 2);
                        localPos += QPoint(0, -10);

                        m_editLine->move(localPos);


                        m_editLine->show();
                        m_editLine->raise();
                        m_editLine->setFocusPolicy(Qt::StrongFocus);
                        m_editLine->setFocus();


                        if (m_aManipulator.IsNull()) {
                            delete m_editLine;
                            m_editLine = nullptr;
                            return;
                        }

                        const gp_Ax2 curAx2AtPopup = m_aManipulator->Position();
                        const gp_Pnt curLocAtPopup = curAx2AtPopup.Location();

                        // 冻结轴索引：优先用 m_distanceAxisIndex，否则用 ActiveAxisIndex
                        int axisIndexFrozen = m_distanceAxisIndex;
                        if (axisIndexFrozen < 0 || axisIndexFrozen > 2) {
                            axisIndexFrozen = m_aManipulator->ActiveAxisIndex();
                        }
                        if (axisIndexFrozen < 0 || axisIndexFrozen > 2) {
                            delete m_editLine;
                            m_editLine = nullptr;
                            return;
                        }

                        // 冻结轴方向
                        gp_Dir axisDirFrozen =
                            (axisIndexFrozen == 0) ? curAx2AtPopup.XDirection() :
                            (axisIndexFrozen == 1) ? curAx2AtPopup.YDirection() :
                            curAx2AtPopup.Direction(); // Z

                        // 用“当前位置 + 旧绝对距离 a”反推绝对起点 anchor：anchor = currentLoc - axisDir * a
                        gp_Pnt anchorFrozen = curLocAtPopup.Translated(gp_Vec(axisDirFrozen) * (-oldDistanceMm));



                        // ---------- 3. editingFinished：把“绝对距离 b”转成增量 (b - a) ----------
                        connect(
                            m_editLine,
                            &QLineEdit::editingFinished,
                            this,
                            [this, oldDistanceMm, axisIndexFrozen, axisDirFrozen, anchorFrozen]()
                            {
                                const QString distanceText = m_editLine->text().trimmed();

                                // 不再依赖m_label
                                if (distanceText.isEmpty()) {
                                    delete m_editLine;
                                    m_editLine = nullptr;
                                    return;
                                }

                                bool ok = false;
                                const double newDistanceMm = distanceText.toDouble(&ok);  // ?? b
                                if (!ok) {
                                    delete m_editLine;
                                    m_editLine = nullptr;
                                    return;
                                }

                                if (m_aManipulator.IsNull()) {
                                    delete m_editLine;
                                    m_editLine = nullptr;
                                    return;
                                }

                                // 当前 manipulator 的坐标系（当前轴向）
                                gp_Ax2 curAx2 = m_aManipulator->Position();

                                // 轴索引：0/1/2 => X/Y/Z
                                int axisIndex = m_distanceAxisIndex;
                                if (axisIndex < 0 || axisIndex > 2) {
                                    axisIndex = m_aManipulator->ActiveAxisIndex();
                                }
                                if (axisIndex < 0 || axisIndex > 2) {
                                    delete m_editLine;
                                    m_editLine = nullptr;
                                    return;
                                }

                                gp_Dir axisDir;
                                if (axisIndex == 0)
                                    axisDir = curAx2.XDirection();
                                else if (axisIndex == 1)
                                    axisDir = curAx2.YDirection();
                                else
                                    axisDir = curAx2.Direction();  // Z

                                // 旧值 a、新值 b（单位：mm）
                                const Standard_Real oldModel = oldDistanceMm;
                                const Standard_Real newModel = newDistanceMm;

                                // 增量：Δ = b - a
                                const Standard_Real deltaModel = newModel - oldModel;
                                if (Abs(deltaModel) < 1e-12) {
                                    delete m_editLine;
                                    m_editLine = nullptr;
                                    return;
                                }

                                // 轴向增量向量（世界坐标）
                                gp_Vec deltaVec(axisDirFrozen);
                                deltaVec *= deltaModel;

                                // 更新 label：显示新的总位移 b
                                {
                                    //QString text = QString("%1 mm").arg(newDistanceMm, 0, 'f', 3);
                                    //m_label->SetText(TCollection_ExtendedString(text.toStdWString().c_str()));
                                    //m_context->Redisplay(m_label, Standard_False);

                                    m_translateDimValueMm = newDistanceMm;

                                }

                                // 1) 先让拖动器也走同一个增量 Δ（不要“拍”到 startPoint+b）
                                curAx2 = m_aManipulator->Position();
                                gp_Pnt curLoc = curAx2.Location();
                                gp_Pnt newLoc = curLoc.Translated(deltaVec);

                                // 保持当前朝向不变，只更新 location
                                gp_Ax2 newAx2(newLoc, curAx2.Direction(), curAx2.XDirection());
                                m_aManipulator->SetPosition(newAx2);

                                // 2) 对物体应用同一个增量 Δ
                                gp_Trsf transformation;
                                transformation.SetTranslation(deltaVec);

                                Handle(AIS_ManipulatorObjectSequence) objects = m_aManipulator->Objects();
                                AIS_ManipulatorObjectSequence::Iterator anObjIter(*objects);
                                for (; anObjIter.More(); anObjIter.Next()) {
                                    const Handle(AIS_InteractiveObject)& anObj = anObjIter.ChangeValue();
                                    gp_Trsf oldTransformation = anObj->Transformation();
                                    const Handle(TopLoc_Datum3D)& aParentTrsf = anObj->CombinedParentTransformation();

                                    if (!aParentTrsf.IsNull() && aParentTrsf->Form() != gp_Identity) {
                                        const gp_Trsf aNewLocalTrsf =
                                            aParentTrsf->Trsf().Inverted()
                                            * transformation
                                            * aParentTrsf->Trsf()
                                            * oldTransformation;
                                        anObj->SetLocalTransformation(aNewLocalTrsf);
                                    }
                                    else {
                                        anObj->SetLocalTransformation(transformation * oldTransformation);
                                    }
                                }

                                // 3) 轨迹线：必须从“绝对起点 0”画到 “0 + b”
                                gp_Pnt startPoint = anchorFrozen;
                                gp_Dir drawAxisDir = axisDirFrozen;


                                gp_Pnt absEndPoint = startPoint.Translated(gp_Vec(drawAxisDir) * newModel);

                                // 先同步缓存：输入框路径与拖动路径统一“绝对起点/轴”
                                m_translateAbsAnchorWorld = anchorFrozen;
                                m_translateAbsAxisWorld = axisDirFrozen;
                                m_translateAbsAxisIndex = axisIndexFrozen;
                                m_hasTranslateAbsAnchor = true;
                                m_distanceAxisIndex = axisIndexFrozen;

                                gp_Ax1 axis1(startPoint, drawAxisDir);
                                ShowTransformTrajectory(m_context, axis1, startPoint, absEndPoint);



                                redrawView();


                                // 收尾
                                delete m_editLine;
                                m_editLine = nullptr;

                            },
                            Qt::UniqueConnection
                        );
                        // -------------------------------------------------
                    }

                    return;
                }

                else if (selected == m_rolabel) {
                    QString currentText = QString::fromUtf16(m_rolabel->Text().ToExtString());

                    QWidget* parentWidget = m_occView->widget()->parentWidget();  // WidgetGuiDocument

                    if (!m_editLine) {
                        m_editLine = new QLineEdit(parentWidget); // 覆盖在 viewer 上

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

                        m_editLine->show();       // 显示
                        m_editLine->raise();      // 放到最上层
                        m_editLine->setFocusPolicy(Qt::StrongFocus);
                        m_editLine->setFocus();   // 获得焦点


                        // 用 rolabel 的 3D 位置投影到屏幕，作为输入框位置
                        if (!m_occView || m_rolabel.IsNull()) {
                            // fallback：退回鼠标位置也行
                        }
                        else {
                            Standard_Integer vx = 0, vy = 0;
                            const gp_Pnt labelPnt = m_rolabel->Position();

                            m_occView->v3dView()->Convert(labelPnt.X(), labelPnt.Y(), labelPnt.Z(), vx, vy);

                            QWidget* viewWidget = m_occView->widget();
                            QWidget* parentWidget = viewWidget->parentWidget();

                            QPoint viewLocalPos(vx, vy);

                            // 如果你发现 y 上下颠倒，启用这一行：
                            // viewLocalPos.setY(viewWidget->height() - vy);

                            QPoint globalPos = viewWidget->mapToGlobal(viewLocalPos);
                            QPoint localPos = parentWidget->mapFromGlobal(globalPos);

                            // 微调：让输入框居中对齐文本
                            localPos -= QPoint(m_editLine->width() / 2, m_editLine->height() / 2);
                            localPos += QPoint(0, -10);

                            m_editLine->move(localPos);
                        }


                        connect(m_editLine, &QLineEdit::editingFinished, this, [this]() {

                            if (!m_editLine || m_aManipulator.IsNull() || m_rolabel.IsNull()) {
                                return;
                            }

                            const QString txt = m_editLine->text().trimmed();
                            bool ok = false;
                            const double angleDeg = txt.toDouble(&ok);
                            if (!ok) {
                                delete m_editLine;
                                m_editLine = nullptr;
                                return;
                            }

                            const Standard_Real angleNew = angleDeg * M_PI / 180.0;

                            // 若冻结状态丢失，兜底从当前 manipulator 取一次（避免输入框直接用到不稳定轴）
                            if (!m_hasRotateAbsAnchor) {
                                const gp_Ax2 ax2 = m_aManipulator->Position();
                                const int idx = m_aManipulator->ActiveAxisIndex();
                                gp_Dir axisDir = (idx == 0) ? ax2.XDirection()
                                    : (idx == 1) ? ax2.YDirection()
                                    : ax2.Direction();

                                m_rotateAbsAnchorWorld = ax2.Location();
                                m_rotateAbsAxisWorld = axisDir;
                                m_rotateAbsAxisIndex = idx;
                                m_rotateAbsAngleRad = 0.0;
                                m_hasRotateAbsAnchor = true;
                            }

                            const Standard_Real delta = angleNew - m_rotateAbsAngleRad;
                            if (std::abs(delta) <= 1e-6) {
                                delete m_editLine;
                                m_editLine = nullptr;
                                return;
                            }

                            // 更新角度文字
                            m_rolabel->SetText(TCollection_ExtendedString(txt.toStdWString().c_str()));
                            m_context->Redisplay(m_rolabel, true);

                            // 用 frozen 的轴 + pivot，构造“唯一的”旋转增量
                            const gp_Ax1 rotAxis(m_rotateAbsAnchorWorld, m_rotateAbsAxisWorld);
                            gp_Trsf rotDelta;
                            rotDelta.SetRotation(rotAxis, delta);

                            // 1) 对象：用同一个 rotDelta 更新（和拖拽一致）
                            Handle(AIS_ManipulatorObjectSequence) objects = m_aManipulator->Objects();
                            AIS_ManipulatorObjectSequence::Iterator it(*objects);
                            for (; it.More(); it.Next()) {
                                const Handle(AIS_InteractiveObject)& obj = it.ChangeValue();
                                const gp_Trsf oldTrsf = obj->Transformation();

                                const Handle(TopLoc_Datum3D)& parent = obj->CombinedParentTransformation();
                                if (!parent.IsNull() && parent->Form() != gp_Identity) {
                                    obj->SetLocalTransformation(parent->Trsf().Inverted() * rotDelta * parent->Trsf() * oldTrsf);
                                }
                                else {
                                    obj->SetLocalTransformation(rotDelta * oldTrsf);
                                }
                            }

                            // 2) 拖动器：同样用 rotDelta 变换它的 gp_Ax2（不要再拆矩阵重建）
                            gp_Ax2 newAx2 = m_aManipulator->Position();
                            newAx2.Transform(rotDelta);
                            m_aManipulator->SetPosition(newAx2);

                            // 3) 更新缓存角度（下一次输入用）
                            m_rotateAbsAngleRad = angleNew;

                            // 4) 轨迹显示：同一根 frozen 轴 
                            ShowRotationTrajectory(m_context, rotAxis, 0.0, angleNew);

                            redrawView();

                            delete m_editLine;
                            m_editLine = nullptr;

                            }, Qt::UniqueConnection);

                    }
                    return;
                }
            }
        }



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