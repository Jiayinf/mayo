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
        m_occView->setColorScaleEnabled(true);
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
        m_aManipulator->Attach(m_aSequence, m_attachOption);

        gp_Ax2 tmpAx2;
        occMatToAx2(mat, tmpAx2, 0);
        m_aManipulator->SetPosition(tmpAx2);

        // 启用指定的操纵模式
        m_aManipulator->EnableMode(AIS_ManipulatorMode::AIS_MM_Translation);  // 启用移动
        m_aManipulator->EnableMode(AIS_ManipulatorMode::AIS_MM_Rotation);     // 启用旋转

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
        m_aManipulator->SetModeActivationOnDetection(Standard_False);

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
        if (!m_rotArc.IsNull()) {
            ctx->Remove(m_rotArc, Standard_False);
            m_rotArc.Nullify();
        }
        
        if (!m_trajectoryShape.IsNull()) {
            ctx->Remove(m_trajectoryShape, Standard_False); // 移除旧轨迹
            m_trajectoryShape.Nullify();
        }

        // 清掉平移的距离标注（辅助直线 + 文字），保证与轨迹同步消失
        if (!m_translateDim.IsNull()) {
            ctx->Remove(m_translateDim, Standard_False);
            m_translateDim.Nullify();
        }

        // 清理 OCC 旋转角度标注与两条线
        if (!m_rotLineBefore.IsNull()) {
            ctx->Remove(m_rotLineBefore, Standard_False);
            m_rotLineBefore.Nullify();
        }
        if (!m_rotLineAfter.IsNull()) {
            ctx->Remove(m_rotLineAfter, Standard_False);
            m_rotLineAfter.Nullify();
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
            ctx->UpdateCurrentViewer(); // 确保 Remove 立即刷新显示
            return;
        }


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



        // 线段长度：优先使用本次旋转会话冻结的尺寸，避免圆弧半径抖动
        Standard_Real lineLen = 120.0;  // fallback
        Standard_Real flyout = 144.0;  // fallback

        if (m_hasRotOverlaySizeFrozen && m_rotOverlayLineLen > 1e-6 && m_rotOverlayFlyout > 1e-6) {
            lineLen = m_rotOverlayLineLen;
            flyout = m_rotOverlayFlyout;
        }
        else {
            // 兜底：如果还没冻结（极少），按旧逻辑算一次
            Standard_Real viewDist = 200.0;
            if (m_occView && !m_occView->v3dView().IsNull() && m_occView->v3dView()->Camera()) {
                viewDist = center.Distance(m_occView->v3dView()->Camera()->Eye());
            }
            lineLen = std::max<Standard_Real>(0.35 * viewDist, 80.0);
            flyout = lineLen * 0.6;
        }


        // 选一个与旋转轴垂直的参考方向 refVec
        // 优先使用本次旋转会话冻结的 m_rotRefDirWorld，确保方向连续，避免圆弧偶发跑到对顶角
        gp_Vec refVec;
        if (m_hasRotRefFrozen) {
            refVec = gp_Vec(m_rotRefDirWorld);

            // 再正交化一次：确保 refVec 严格位于“垂直旋转轴”的平面内（防止数值漂移）
            refVec = refVec - gp_Vec(axisDir) * refVec.Dot(gp_Vec(axisDir));
            if (refVec.SquareMagnitude() < 1e-12) {
                // 极端退化：fallback
                refVec = gp_Vec(axisDir).Crossed(gp_Vec(0, 0, 1));
                if (refVec.SquareMagnitude() < 1e-12) {
                    refVec = gp_Vec(axisDir).Crossed(gp_Vec(1, 0, 0));
                }
            }
            refVec.Normalize();
        }
        else {
            // 未冻结时才用 cross 方案
            refVec = gp_Vec(axisDir).Crossed(gp_Vec(0, 0, 1));
            if (refVec.SquareMagnitude() < 1e-12) {
                refVec = gp_Vec(axisDir).Crossed(gp_Vec(1, 0, 0));
            }
            refVec.Normalize();
        }


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


        // ==========================================================
        // 【替代方案】自绘固定半径圆弧（不使用 PrsDim_AngleDimension，彻底去掉外圈不可选数字）
        // ==========================================================

        auto normToPi = [](double a) {
            while (a > M_PI) a -= 2.0 * M_PI;
            while (a < -M_PI) a += 2.0 * M_PI;
            return a;
            };

        // 用同一套“取 (-pi, pi]”的角度差，保证弧线和内侧数字一致
        const double delta = normToPi(endAngle - startAngle);
        const double arcEndAngle = startAngle + delta;

        // 画圆弧：圆所在坐标系（原点=center，法向=axisDir，X方向=refVec）
        gp_Ax2 arcAx2(center, axisDir, gp_Dir(refVec));
        Handle(Geom_Circle) circle = new Geom_Circle(arcAx2, flyout);

        // trimmed 参数区间 & 方向（delta 可能为负）
        Standard_Real u1 = static_cast<Standard_Real>(startAngle);
        Standard_Real u2 = static_cast<Standard_Real>(arcEndAngle);
        Standard_Boolean sense = Standard_True;
        if (u2 < u1) {
            std::swap(u1, u2);
            sense = Standard_False;
        }

        Handle(Geom_TrimmedCurve) arcCrv = new Geom_TrimmedCurve(circle, u1, u2, sense);
        TopoDS_Edge arcEdge = BRepBuilderAPI_MakeEdge(arcCrv);

        // 显示圆弧
        m_rotArc = new AIS_Shape(arcEdge);
        m_rotArc->SetColor(trajColor);
        m_rotArc->SetWidth(2.0);
        m_rotArc->SetZLayer(Graphic3d_ZLayerId_Topmost);
        ctx->SetDisplayPriority(m_rotArc, 12);
        ctx->Display(m_rotArc, Standard_False);

        // ----------------------------------------------------------
        // 下面继续沿用你原逻辑：计算文字位置 textPos / signedAngleRad
        // ----------------------------------------------------------

        const double midAngle = startAngle + 0.5 * delta;

        gp_Vec vMid = v0;
        vMid.Rotate(rotationAxis, midAngle);
        if (vMid.SquareMagnitude() > 1e-12) {
            vMid.Normalize();
        }

        // 文字位置（仍放在弧内侧）
        const Standard_Real textRadius = flyout * 0.72; // 0.65~0.85 自行微调
        gp_Pnt textPos = center.Translated(vMid * textRadius);

        // 角度数值（与弧一致）
        double signedAngleRad = delta;
        if (std::abs(signedAngleRad) < 1e-10) signedAngleRad = 0.0;




        // -------------------------------------------------------
        // 用 AIS_TextLabel 显示“角度数值”，并作为可点击入口（恢复原先可用的交互方式）
        // 注意：文本必须是纯数字（不要加 "deg"），否则 editingFinished 里 toDouble 会失败
        // -------------------------------------------------------
        
        {
            const double signedAngleDeg = signedAngleRad * 180.0 / M_PI;
            QString angleText = QString::number(signedAngleDeg, 'f', 3); // 精度按需调

            m_rolabel = new AIS_TextLabel();
            m_rolabel->SetText(TCollection_ExtendedString(angleText.toStdWString().c_str()));
            m_rolabel->SetPosition(textPos);
            m_rolabel->SetColor(trajColor);
            m_rolabel->SetZLayer(Graphic3d_ZLayerId_Topmost);

            ctx->SetDisplayPriority(m_rolabel, 13);
            ctx->Display(m_rolabel, Standard_False);

            // 让它可选（这样你 release 里 InitSelected/SelectedInteractive 才能拿到它）
            ctx->Activate(m_rolabel, 0, Standard_True);
            ctx->SetSelectionSensitivity(m_rolabel, 0, 8);
        }

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
        // 【新增】开始平移轨迹时，必须清理旋转辅助线与角度标注
        if (!m_rotLineBefore.IsNull()) {
            ctx->Remove(m_rotLineBefore, Standard_False);
            m_rotLineBefore.Nullify();
        }
        if (!m_rotLineAfter.IsNull()) {
            ctx->Remove(m_rotLineAfter, Standard_False);
            m_rotLineAfter.Nullify();
        }


        if (!m_rotArc.IsNull()) {
            ctx->Remove(m_rotArc, Standard_False);
            m_rotArc.Nullify();
        }
        if (!m_rolabel.IsNull()) {
            ctx->Remove(m_rolabel, Standard_False);
            m_rolabel.Nullify();
        }


        
        // 在更新轨迹的函数中：
        if (!m_trajectoryShape.IsNull()) {
            ctx->Remove(m_trajectoryShape, Standard_False); // 移除旧轨迹
            m_trajectoryShape.Nullify();
        }


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

        if (!m_translateDim.IsNull()) {
            ctx->Remove(m_translateDim, Standard_False);
            m_translateDim.Nullify();
        }

        if (!arrowStart.IsNull()) {
            ctx->Remove(arrowStart, Standard_False);
            arrowStart.Nullify();
        }

        if (!arrowEnd.IsNull()) {
            ctx->Remove(arrowEnd, Standard_False);
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


        // =======================12.26 =======================
        // ======= Simple dimension label (parallel dimension line next to trajectory) =======
        // Keep a cache value (optional, can be used later for edit-box prefill)
        m_translateDimValueMm = signedDistance;


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
        m_hasRotOverlaySizeFrozen = false;
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

        // 【新增】如果是单击旋转角度文字（m_rolabel），不要启动操纵器变换，否则 release 时可能触发 StopTransform 导致 overlay 消失
        if (event->button() == Qt::LeftButton && !m_rolabel.IsNull() && m_context && m_occView && m_occView->v3dView()) {
            // 让 OCC 做一次检测
            m_context->MoveTo(currPos.x(), currPos.y(), m_occView->v3dView(), Standard_True);
            if (m_context->HasDetected()) {
                Handle(AIS_InteractiveObject) detected = m_context->DetectedInteractive();
                if (detected == m_rolabel) {
                    m_pendingRotLabelClick = true;
                    return; // 关键：不走 StartTransform，不走 m_aManipulatorDo 的逻辑
                }
            }
        }


        if (m_aManipulatorReady)
        {
            int currentOperation = -1;
            m_aManipulatorDo = true;
            m_aManipulator->SetModeActivationOnDetection(Standard_True);

            if (m_aManipulatorDo && m_aManipulator->HasActiveMode())
            {
                int tmpActiveAxisIndex = m_aManipulator->ActiveAxisIndex();
                int tmpActiveAxisMode = m_aManipulator->ActiveMode();

                auto clearRotationOverlay = [&](const Handle(AIS_InteractiveContext)& ctx) {
                    if (ctx.IsNull()) return;

                    if (!m_rotArc.IsNull()) {
                        ctx->Remove(m_rotArc, Standard_False);
                        m_rotArc.Nullify();
                    }
                    if (!m_rotLineBefore.IsNull()) {
                        ctx->Remove(m_rotLineBefore, Standard_False);
                        m_rotLineBefore.Nullify();
                    }
                    if (!m_rotLineAfter.IsNull()) {
                        ctx->Remove(m_rotLineAfter, Standard_False);
                        m_rotLineAfter.Nullify();
                    }

                    if (!m_rolabel.IsNull()) {
                        ctx->Remove(m_rolabel, Standard_False);
                        m_rolabel.Nullify();
                    }

                    ctx->UpdateCurrentViewer(); // 关键：立刻刷新
                    };


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

                    // 【新增】一进入平移模式就清理上一帧旋转轨迹（避免“先旋转再平移”残留）
                    if (m_lastOperation >= 4 && m_lastOperation <= 6) { // 4/5/6 对应旋转三轴
                        clearRotationOverlay(m_context);
                    }

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


                    // 平移：只更新平移“绝对起点/轴”
                    m_translateAbsAnchorWorld = ax2.Location();
                    m_translateAbsAxisWorld = axisDir;
                    m_translateAbsAxisIndex = tmpActiveAxisIndex;
                    m_hasTranslateAbsAnchor = true;

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

        if (m_editLine && m_editLine->isVisible()) {
            return;
        }

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

                                            // 冻结起点应取“当前拖动器位置”，而不是旧缓存 m_initialPosition
                                            m_translateAbsAnchorWorld = manipAx2.Location();

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

                                // 1) 取稳定参考方向：优先用你已冻结的 m_rotRefDirWorld
                                gp_Vec refVec;
                                if (m_hasRotRefFrozen) {
                                    refVec = gp_Vec(m_rotRefDirWorld);
                                }
                                else {
                                    // 兜底：找一个与当前轴垂直的向量
                                    refVec = gp_Vec(axisRotation).Crossed(gp_Vec(0, 0, 1));
                                    if (refVec.SquareMagnitude() < 1e-12) {
                                        refVec = gp_Vec(axisRotation).Crossed(gp_Vec(1, 0, 0));
                                    }
                                }
                                if (refVec.SquareMagnitude() > 1e-12) refVec.Normalize();

                                // 2) 用 deltaRotation 的等价轴角旋转 refVec，得到 refRot
                                gp_Vec refRot = refVec;
                                if (qAxis.SquareMagnitude() > 1e-12 && std::abs(qAngle) > 1e-12) {
                                    gp_Ax1 axTmp(m_rotateAbsAnchorWorld, gp_Dir(qAxis));
                                    // curLoc 取你已有的 pivot（你下面就有 curLoc）
                                    refRot.Rotate(axTmp, qAngle);
                                    if (refRot.SquareMagnitude() > 1e-12) refRot.Normalize();
                                }

                                // 3) 用 atan2 得到可正可负、且“能回退变短”的角度（范围 [-pi, pi]）
                                double signedAngle = std::atan2(
                                    axisRotation.Dot(refVec.Crossed(refRot)),
                                    refVec.Dot(refRot)
                                );


                                // 【改】冻结/刷新 pivot：除了 axisIndex 变化，还要检测“操纵器位置是否变了”
                                const gp_Pnt curLoc = m_aManipulator->Position().Location();
                                const gp_Dir curAxisDir(axisRotation);

                                // 位置阈值：按你的单位精度可微调（这里用 1e-6 够用）
                                const bool pivotMoved = m_hasRotateAbsAnchor
                                    ? (curLoc.Distance(m_rotateAbsAnchorWorld) > 1e-6)
                                    : true;

                                // 轴向阈值：dot 接近 1 表示方向一致
                                const bool axisChanged = m_hasRotateAbsAnchor
                                    ? (std::abs(curAxisDir.Dot(m_rotateAbsAxisWorld)) < 0.9999)
                                    : true;

                                if (!m_hasRotateAbsAnchor
                                    || m_rotateAbsAxisIndex != tmpActiveAxisIndex
                                    || pivotMoved
                                    || axisChanged)
                                {
                                    m_rotateAbsAnchorWorld = curLoc;
                                    m_rotateAbsAxisWorld = curAxisDir;
                                    m_rotateAbsAxisIndex = tmpActiveAxisIndex;
                                    m_hasRotateAbsAnchor = true;
                                    m_rotateAbsAngleRad = 0.0;


                                    // -----------------------------
                                    // 【新增】冻结叠加层尺寸：lineLen / flyout
                                    // 只在本次“旋转会话 pivot/axis 发生刷新”时算一次
                                    // -----------------------------
                                    Standard_Real viewDist = 200.0;
                                    if (m_occView && !m_occView->v3dView().IsNull() && m_occView->v3dView()->Camera()) {
                                        viewDist = m_rotateAbsAnchorWorld.Distance(m_occView->v3dView()->Camera()->Eye());
                                    }

                                    m_rotOverlayLineLen = std::max<Standard_Real>(0.35 * viewDist, 80.0);
                                    m_rotOverlayFlyout = m_rotOverlayLineLen * 0.8;   // 你现在用 1.2，就沿用
                                    m_hasRotOverlaySizeFrozen = true;


                                    // -----------------------------
                                    // 【新增】冻结“参考轴方向”
                                    // 规则：ref = (rot+1)%3  => X->Y, Y->Z(蓝), Z->X
                                    // 这样：绕绿色(Y=1)旋转时，永远取蓝色(Z=2)作为参考
                                    // -----------------------------
                                    m_rotRefRotAxisIndex = tmpActiveAxisIndex;
                                    m_rotRefAxisIndex = (tmpActiveAxisIndex + 1) % 3;

                                    const gp_Ax2 ax2Ref = m_aManipulator->Position();
                                    gp_Dir refDir =
                                        (m_rotRefAxisIndex == 0) ? ax2Ref.XDirection() :
                                        (m_rotRefAxisIndex == 1) ? ax2Ref.YDirection() :
                                        ax2Ref.Direction(); // Z

                                    // 正交化：保证 refDir 严格在“垂直旋转轴”的平面内
                                    gp_Vec vRef(refDir);
                                    vRef = vRef - gp_Vec(curAxisDir) * vRef.Dot(gp_Vec(curAxisDir));
                                    if (vRef.SquareMagnitude() < 1e-12) {
                                        // 兜底（理论上不会发生）：再找一个不平行的向量
                                        vRef = gp_Vec(curAxisDir).Crossed(gp_Vec(0, 0, 1));
                                        if (vRef.SquareMagnitude() < 1e-12) vRef = gp_Vec(curAxisDir).Crossed(gp_Vec(1, 0, 0));
                                    }
                                    vRef.Normalize();

                                    // 防翻面：如果之前已经冻结且是同一根旋转轴，保持方向连续（避免黑线突然反向）
                                    if (m_hasRotRefFrozen && m_rotRefRotAxisIndex == tmpActiveAxisIndex) {
                                        if (gp_Vec(m_rotRefDirWorld).Dot(vRef) < 0.0) vRef.Reverse();
                                    }

                                    m_rotRefDirWorld = gp_Dir(vRef);
                                    m_hasRotRefFrozen = true;

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
                Handle(AIS_InteractiveObject) selected = m_context->SelectedInteractive();

                // 【新增】如果 press 阶段判定点到了角度标签，则 release 阶段强制按 m_rolabel 处理
                if (m_pendingRotLabelClick) {
                    selected = m_rolabel;
                    m_pendingRotLabelClick = false;
                }



               // 处理距离文本（平移）输入框
                if (selected == m_translateDim) {

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

                    // -------------------------------------------------------
                    // 【新增】冻结本次输入框要用的旋转轴/中心/旧角度（防止提交时串轴）
                    // 必须在弹出输入框时冻结，而不是在 editingFinished 时再去读全局缓存
                    // -------------------------------------------------------

                    if (!m_hasRotateAbsAnchor) {
                        // 兜底：如果全局缓存丢了，就从当前操纵器取一个合理值
                        const gp_Ax2 ax2 = m_aManipulator->Position();
                        int idx = m_aManipulator->ActiveAxisIndex();
                        if (idx < 0 || idx > 2) idx = 2;

                        gp_Dir axisDir = (idx == 0) ? ax2.XDirection()
                            : (idx == 1) ? ax2.YDirection()
                            : ax2.Direction();

                        m_rotateAbsAnchorWorld = ax2.Location();
                        m_rotateAbsAxisWorld = axisDir;
                        m_rotateAbsAxisIndex = idx;
                        m_rotateAbsAngleRad = 0.0;
                        m_hasRotateAbsAnchor = true;
                    }

                    // 冻结本次编辑会话
                    m_rotEditAnchorWorld = m_rotateAbsAnchorWorld;
                    m_rotEditAxisWorld = m_rotateAbsAxisWorld;
                    m_rotEditAxisIndex = m_rotateAbsAxisIndex;
                    m_rotEditOldAngleRad = m_rotateAbsAngleRad;
                    m_hasRotEditFrozen = true;

                    QWidget* parentWidget = m_occView->widget()->parentWidget();  // WidgetGuiDocument

                    if (!m_editLine) {
                        m_editLine = new QLineEdit(parentWidget); // 覆盖在 viewer 上


                        m_editLine->setStyleSheet("background: white; color: black; border: 1px solid red;");
                        m_editLine->setAlignment(Qt::AlignCenter);
                        m_editLine->setValidator(new QRegularExpressionValidator(QRegularExpression("^-?(360(\\.0+)?|([1-9]?\\d|[1-2]\\d{2}|3[0-5]\\d|359)(\\.\\d+)?|0(\\.\\d+)?)$")));
                        m_editLine->resize(80, 24);
                        m_editLine->setFrame(true);
                        m_editLine->hide();


                        // 设置文本框位置
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

                            // 没有冻结态就不允许提交（避免串轴）
                            if (!m_hasRotEditFrozen) {
                                delete m_editLine;
                                m_editLine = nullptr;
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

                            // delta 必须相对“弹出输入框那一刻的旧角度”
                            const Standard_Real delta = angleNew - m_rotEditOldAngleRad;
                            if (std::abs(delta) <= 1e-6) {
                                delete m_editLine;
                                m_editLine = nullptr;
                                m_hasRotEditFrozen = false;
                                return;
                            }

                            // 更新角度文字
                            m_rolabel->SetText(TCollection_ExtendedString(txt.toStdWString().c_str()));
                            m_context->Redisplay(m_rolabel, true);

                            // 旋转轴必须用“本次冻结”的轴/中心（不会串到绿轴）
                            const gp_Ax1 rotAxis(m_rotEditAnchorWorld, m_rotEditAxisWorld);
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

                            // 2) 拖动器：同样用 rotDelta 变换它的 gp_Ax2
                            gp_Ax2 newAx2 = m_aManipulator->Position();
                            newAx2.Transform(rotDelta);
                            m_aManipulator->SetPosition(newAx2);

                            // 3) 同步全局缓存（轴/中心保持为本次冻结的那根）
                            m_rotateAbsAnchorWorld = m_rotEditAnchorWorld;
                            m_rotateAbsAxisWorld = m_rotEditAxisWorld;
                            m_rotateAbsAxisIndex = m_rotEditAxisIndex;
                            m_hasRotateAbsAnchor = true;
                            m_rotateAbsAngleRad = angleNew;

                            // 旋转后建议让平移冻结失效（你之前的平移起点问题）
                            m_hasTranslateAbsAnchor = false;
                            m_translateAbsAxisIndex = -1;
                            m_lastOperation = -1;

                            // 4) 轨迹显示：同一根冻结轴
                            ShowRotationTrajectory(m_context, rotAxis, 0.0, angleNew);

                            redrawView();

                            m_hasRotEditFrozen = false;

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
            {

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