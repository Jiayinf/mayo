#pragma once

#include "../base/span.h"
#include "../gui/v3d_view_controller.h"
#include "../graphics/graphics_view_ptr.h"
#include "view3d_navigation_style.h"

#include <AIS_Manipulator.hxx>
#include <AIS_Shape.hxx>
#include <AIS_TextLabel.hxx>
#include <PrsDim_LengthDimension.hxx>
#include <gp_Pnt.hxx>

#include <QtCore/QObject>
#include <QtCore/QPoint>
#include <functional>
#include <memory>
#include <vector>
#include <QLineEdit>
#include <QWidget>


#include <PrsDim_AngleDimension.hxx>   
#include <gp_Dir.hxx>   
#include <gp_Vec.hxx>


class QCursor;
class QKeyEvent;
class QMouseEvent;
class QRubberBand;
class QWheelEvent;
class SoDragger;

namespace Mayo {

    class IWidgetOccView;

    class WidgetOccViewController : public QObject, public V3dViewController {
        Q_OBJECT
    public:
        WidgetOccViewController(IWidgetOccView* occView = nullptr, AIS_InteractiveContext* context = nullptr);

        bool eventFilter(QObject* watched, QEvent* event) override;

        void setNavigationStyle(View3dNavigationStyle style);

        void startManipulator(GraphicsObjectPtr object, float mat[12]);

        void startManipulator(std::vector<GraphicsObjectPtr>& gfxObjects);

        void setManipulatorReady(bool ready);

        bool getManipulatorReady();

        void stopManipulator();

        gp_Pnt getTransform();

        void ShowRotationTrajectory(const Handle(AIS_InteractiveContext)& ctx, const gp_Ax1& rotationAxis, double startAngle, double endAngle);

        void ShowTransformTrajectory(const Handle(AIS_InteractiveContext)& ctx, const gp_Ax1& rotationAxis, gp_Pnt startPoint, gp_Pnt endPoint);


        void printWidgetTree(QWidget* widget, int depth = 0);

    protected:
        void redrawView() override;

    private:
        static Position toPosition(const QPoint& pnt) { return { pnt.x(), pnt.y() }; }
        static QPoint toQPoint(const Position& pos) { return { pos.x, pos.y }; }

        void startDynamicAction(DynamicAction action) override;
        void stopDynamicAction() override;

        void setViewCursor(const QCursor& cursor);

        std::unique_ptr<IRubberBand> createRubberBand() override;
        struct RubberBand;

        void handleEvent(const QEvent* event);
        void handleKeyPress(const QKeyEvent* event);
        void handleKeyRelease(const QKeyEvent* event);
        void handleMouseButtonPress(const QMouseEvent* event);
        void handleMouseMove(const QMouseEvent* event);
        void handleMouseButtonRelease(const QMouseEvent* event);
        void handleMouseWheel(const QWheelEvent* event);

        Quantity_Color colorFromAxisIndex(int axisIndex);


        // -- Action matching

        // User input: key, mouse button, ...
        using Input = int;

        // Sequence of user inputs being "on" : key pressed, mouse button pressed, ...
        class InputSequence {
        public:
            void push(Input in);
            void release(Input in);
            void clear();
            Span<const Input> data() const { return m_inputs; }

            enum class Operation { None, Push, Release };
            Operation lastOperation() const { return m_lastOperation; }
            Input lastInput() const { return m_lastInput; }

            bool equal(std::initializer_list<Input> other) const;

            void setPrePushCallback(std::function<void(Input)> fn) { m_fnPrePushCallback = std::move(fn); }
            void setPreReleaseCallback(std::function<void(Input)> fn) { m_fnPreReleaseCallback = std::move(fn); }
            void setClearCallback(std::function<void()> fn) { m_fnClearCallback = std::move(fn); }

        private:
            std::vector<Input> m_inputs;
            Operation m_lastOperation = Operation::None;
            Input m_lastInput = -1;
            std::function<void(Input)> m_fnPrePushCallback;
            std::function<void(Input)> m_fnPreReleaseCallback;
            std::function<void()> m_fnClearCallback;
        };

        // Base class to provide matching of DynamicAction from an InputSequence object
        class ActionMatcher {
        public:
            ActionMatcher(const InputSequence* seq) : inputs(*seq) {}
            virtual ~ActionMatcher() = default;

            virtual bool matchRotation() const = 0;
            virtual bool matchPan() const = 0;
            virtual bool matchZoom() const = 0;
            virtual bool matchWindowZoom() const = 0;

            virtual void onInputPrePush(Input) {}
            virtual void onInputPreRelease(Input) {}
            virtual void onInputCleared() {}

            const InputSequence& inputs;
        };

        // Fabrication to create corresponding ActionMatcher from navigation style
        static std::unique_ptr<ActionMatcher> createActionMatcher(View3dNavigationStyle style, const InputSequence* seq);
        class Mayo_ActionMatcher;
        class Catia_ActionMatcher;
        class SolidWorks_ActionMatcher;
        class Unigraphics_ActionMatcher;
        class ProEngineer_ActionMatcher;

        // -- Attributes

        IWidgetOccView* m_occView = nullptr;
        AIS_InteractiveContext* m_context = nullptr;
        Position m_prevPos;
        gp_Pnt m_initialPosition;
        gp_Trsf m_initialRotation;

        // 1/2/3 = 平移 X/Y/Z（Translation）
        // 4/5/6 = 旋转 X/Y/Z（Rotation）
        // 用于：模式切换时清理上一种 overlay；以及输入框会话冻结/恢复逻辑
        int m_lastOperation = -1;

        Handle(AIS_Shape) m_trajectoryShape = nullptr;
        Handle(AIS_TextLabel) m_rolabel = nullptr;
        // 平移距离尺寸标注（独立于轨迹线的标注辅助线+箭头+文字）
        Handle(PrsDim_LengthDimension) m_translateDim = nullptr;
        //Handle(AIS_Shape) m_rotArc;   // 【新增】自己画的旋转圆弧（固定半径）

        // 旋转显示：两条参考线 + 角度标注（OCC）
        Handle(AIS_Shape) m_rotLineBefore;                  // 旋转前参考线
        Handle(AIS_Shape) m_rotLineAfter;                   // 旋转后参考线
        Handle(AIS_Shape) m_rotArc;                 // 【新增】自绘圆弧



        // 缓存当前平移距离（用于双击标注时预填输入框）
        double m_translateDimValueMm = 0.0;


        Handle(AIS_Shape) arrowStart = nullptr;
        Handle(AIS_Shape) arrowEnd = nullptr;
        QLineEdit* m_editLine = nullptr;

        bool m_pendingRotLabelClick = false;


        View3dNavigationStyle m_navigStyle = View3dNavigationStyle::Mayo;
        InputSequence m_inputSequence;
        std::unique_ptr<ActionMatcher> m_actionMatcher;

        Handle(AIS_ManipulatorObjectSequence) m_aSequence;
        Handle(AIS_Manipulator) m_aManipulator;
        bool m_aManipulatorReady;
        bool m_aManipulatorDo;
        AIS_Manipulator::OptionsForAttach m_attachOption;
        int m_meshId;

    private:
        /*static void dragStartCallback(void* data, SoDragger* d);
        static void dragFinishCallback(void* data, SoDragger* d);
        static void dragMotionCallback(void* data, SoDragger* d);*/
        gp_Pnt m_posTransform;

        gp_XYZ m_axis;

        int m_distanceAxisIndex = -1;  // 记录当前 distance 对应的平移轴（0=X,1=Y,2=Z）

        // --- 平移辅助线的绝对起点 ---
        gp_Pnt m_translateAbsAnchorWorld;
        gp_Dir m_translateAbsAxisWorld;
        int    m_translateAbsAxisIndex = -1;   // 0/1/2
        bool   m_hasTranslateAbsAnchor = false;


        // --- 旋转辅助线的绝对起点 ---
        bool         m_hasRotateAbsAnchor = false;
        gp_Pnt       m_rotateAbsAnchorWorld;   // pivot（通常就是 manipulator 的 Location）
        gp_Dir       m_rotateAbsAxisWorld;     // 当前冻结的旋转轴方向（世界坐标系）
        int          m_rotateAbsAxisIndex = -1;
        Standard_Real m_rotateAbsAngleRad = 0.0; // 当前累计角（带符号，rad）

        // --- 旋转输入框会话冻结态（关键：避免输入框提交时串到别的轴） ---
        bool         m_hasRotEditFrozen = false;
        gp_Pnt       m_rotEditAnchorWorld;
        gp_Dir       m_rotEditAxisWorld;
        int          m_rotEditAxisIndex = -1;
        Standard_Real m_rotEditOldAngleRad = 0.0;



        // 旋转“参考轴”冻结：保证黑色起始线永远沿“本次旋转开始前”的参考轴正方向（且不翻转）
        bool  m_hasRotRefFrozen = false;
        
        int   m_rotRefRotAxisIndex = -1;     // 当前绕哪根轴旋转(0/1/2)
        int   m_rotRefAxisIndex = -1;        // 用哪根轴做参考线(0/1/2) 例：绕绿Y -> 参考蓝Z


        gp_Dir m_rotRefDirWorld;              // 参考轴在世界坐标下的方向（冻结）

        // --- 旋转叠加层尺寸冻结（避免 flyout/半径 抖动） ---
        bool        m_hasRotOverlaySizeFrozen = false;
        Standard_Real m_rotOverlayLineLen = 0.0;
        Standard_Real m_rotOverlayFlyout = 0.0;




    };

} // namespace Mayo