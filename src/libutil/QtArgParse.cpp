// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio

#include <OpenImageIO/argparse.h>
#include <OpenImageIO/strutil.h>

#include "QtArgParse.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>



OIIO_NAMESPACE_BEGIN

namespace Strutil {
// template<typename T> inline std::string to_string (const T& value);
template<>
inline std::string
to_string(const QString& value)
{
    return value.toUtf8().data();
}
}  // end namespace Strutil



/// QFloatSlider is an improved QSlider with a floating point range.
///
/// * Default range is -1e6 to 1e6 (allows negatives and bigger values than
///   regular QSlider, unless you restrict the range further).
/// * Default single-step size is not fixed, but gets bigger or smaller
///   based on the current value. That makes it easier to make fine
///   adjustments especially near zero.
/// * KeyboardTracking turned off by default, so if you edit the value as
///   text, the change doesn't take effect until you hit enter or focus
///   on a different widget.
class QFloatSlider : public QSlider {
public:
    typedef QSlider parent_t;
    static const int imax = 1000;

    QFloatSlider(Qt::Orientation orientation, QWidget* parent = nullptr)
        : QSlider(orientation, parent)
    {
        parent_t::setRange(0, imax);  // underlying int range
        setRange(0.0f, 100.0f);       // float range
        setValue(0.0f);
        setMaximumWidth(200);
        setMinimumWidth(200);
    }

    void setRange(float min, float max)
    {
        m_fmin   = min;
        m_fmax   = max;
        m_frange = max - min;
    }

    int to_int(float v) const { return int(imax * (v - m_fmin) / m_frange); }

    float to_float(int i) const
    {
        float v = float(i) / (imax);
        return m_fmin * (1.0f - v) + m_fmax * v;
    }

    void setValue(float v) { parent_t::setValue(to_int(v)); }

    float value() const { return to_float(parent_t::value()); }

private:
    float m_fmin;
    float m_fmax;
    float m_frange;
};



QtArgParse::QtArgParse(ArgParse& ap, QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(ap.prog_name().c_str());
    auto display_area = new QWidget(this);
    // auto display_area_layout = new QVBoxLayout;
    auto display_area_layout = new QGridLayout;
    display_area->setLayout(display_area_layout);
    setCentralWidget(display_area);

    using Strutil::print;
    using Strutil::fmt::format;

    int n = ap.get_narguments();
    for (int i = 0; i < n; ++i) {
        const ArgParse::Arg* arg = ap.get_argument(i);
        std::string dest         = arg->dest();
        auto val                 = ap.params()[dest];
        if (arg->name() == "help")
            continue;
        if (arg->is_hidden() || arg->uihint("hidden").get<int>())
            continue;
        if (arg->is_separator())
            print("\t[separator]\n");
        std::string widget = arg->uihint("widget");
        std::string label  = arg->uihint("label").get(arg->name());

        if (arg->is_bool() || widget == "checkbox") {
            std::string text = format("{} (-{})", label, arg->name());
            auto w           = new QCheckBox(text.c_str(), display_area);
            display_area_layout->addWidget(w, i, 0, 1, 2);
            w->setChecked(val.get<int>());
            if (label != arg->help())
                w->setToolTip(arg->help().c_str());
            connect(w, &QCheckBox::stateChanged, this, [&, dest](int state) {
                ap.params()[dest] = state ? 1 : 0;
            });

        } else if (val.type() == TypeInt) {
            auto w = new QLabel(label.c_str(), display_area);
            display_area_layout->addWidget(w, i, 0);
            w->setToolTip(arg->help().c_str());
            QLineEdit* ed     = nullptr;
            QSlider* slider   = nullptr;
            QSpinBox* spinbox = nullptr;
            if (widget != "spinbox") {
                ed = new QLineEdit(val.get().c_str(), display_area);
                display_area_layout->addWidget(ed, i, 1);
                ed->setToolTip(arg->help().c_str());
            }
            if (widget == "slider") {
                slider = new QSlider(Qt::Horizontal);
                display_area_layout->addWidget(slider, i, 2);
                slider->setRange(arg->uihint("min").get<int>(0),
                                 arg->uihint("max").get<int>(100));
                slider->setValue(val.get<int>());
                slider->setTracking(true);
            }
            if (widget == "spinbox") {
                spinbox = new QSpinBox;
                display_area_layout->addWidget(spinbox, i, 1);
                spinbox->setRange(arg->uihint("min").get<int>(0),
                                  arg->uihint("max").get<int>(100));
                spinbox->setValue(val.get<int>());
            }
            if (ed)
                connect(ed, &QLineEdit::editingFinished, this, [=, &ap]() {
                    auto text         = Strutil::to_string(ed->text());
                    int v             = Strutil::from_string<int>(text);
                    ap.params()[dest] = v;
                    if (slider)
                        slider->setValue(v);
                });
            if (slider)
                connect(slider, &QSlider::valueChanged, this,
                        [=, &ap](int newval) {
                            ap.params()[dest] = newval;
                            if (ed)
                                ed->setText(Strutil::to_string(newval).c_str());
                        });
            if (spinbox)
                connect<void (QSpinBox::*)(int)>(spinbox,
                                                 &QSpinBox::valueChanged, this,
                                                 [=, &ap](int newval) {
                                                     ap.params()[dest] = newval;
                                                 });

        } else if (val.type() == TypeFloat) {
            auto w = new QLabel(label.c_str(), display_area);
            display_area_layout->addWidget(w, i, 0);
            w->setToolTip(arg->help().c_str());
            auto ed = new QLineEdit(val.get().c_str(), display_area);
            display_area_layout->addWidget(ed, i, 1);
            ed->setToolTip(arg->help().c_str());
            QFloatSlider* slider = nullptr;
            if (widget == "slider"
                || (arg->uihint("min").type() && arg->uihint("max").type())) {
                slider = new QFloatSlider(Qt::Horizontal);
                display_area_layout->addWidget(slider, i, 2);
                slider->setRange(arg->uihint("min").get<float>(0.0f),
                                 arg->uihint("max").get<float>(100.0f));
                slider->setValue(val.get<float>());
                slider->setTracking(true);
                slider->setToolTip(arg->help().c_str());
            }
            connect(ed, &QLineEdit::editingFinished, this,
                    [&ap, dest, ed, slider]() {
                        auto text         = Strutil::to_string(ed->text());
                        int v             = Strutil::from_string<float>(text);
                        ap.params()[dest] = v;
                        if (slider)
                            slider->setValue(v);
                    });
            if (slider)
                connect(slider, &QSlider::valueChanged, this,
                        [&ap, dest, ed, slider](float) {
                            float newval      = slider->value();
                            ap.params()[dest] = newval;
                            ed->setText(
                                Strutil::sprintf("%.1f", newval).c_str());
                        });


        } else if (val.type() == TypeString) {
            auto w = new QLabel(label.c_str(), display_area);
            display_area_layout->addWidget(w, i, 0);
            w->setToolTip(arg->help().c_str());
            auto ed = new QLineEdit(val.get().c_str(), display_area);
            display_area_layout->addWidget(ed, i, 1);
            ed->setToolTip(arg->help().c_str());
            connect(ed, &QLineEdit::editingFinished, this, [=, &ap]() {
                ap.params()[dest] = Strutil::to_string(ed->text());
            });

        } else {
            std::string text = format("{} = {} ({})", arg->name(), val.get(),
                                      val.type());
            auto w           = new QLabel(text.c_str(), display_area);
            display_area_layout->addWidget(w, i, 0, 1, 2);
            w->setToolTip(arg->help().c_str());
            // print("\tvalue ({}) = {}\n", val.type(), val.get());
        }
    }
    {
        // Create and position a button
        auto button = new QPushButton("GO!", display_area);
        // button->setGeometry(10, 10, 80, 30);
        connect(button, &QPushButton::clicked, QApplication::instance(),
                &QApplication::quit);
        display_area_layout->addWidget(button, n, 0);
    }
}



int
ArgParse::gui(int argc, const char** argv)
{
    QApplication app(argc, (char**)argv);
    QtArgParse win(*this);
    win.show();
    int r = app.exec();

#if 1
    using Strutil::print;
    print("current params:\n");
    for (const auto& p : params()) {
        print("\t{} {} = {}\n", p.type(), p.name(), p.get_string());
    }
#endif

    return r;
}

OIIO_NAMESPACE_END
