// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio

#if defined(_MSC_VER)
// Ignore warnings about conditional expressions that always evaluate true
// on a given platform but may evaluate differently on another. There's
// nothing wrong with such conditionals.
// Also ignore warnings about not being able to generate default assignment
// operators for some Qt classes included in headers below.
#    pragma warning(disable : 4127 4512)
#endif


#include <QMainWindow>

#include <OpenImageIO/argparse.h>
using namespace OIIO;



OIIO_NAMESPACE_BEGIN

class QtArgParse : public QMainWindow {
    Q_OBJECT
public:
    explicit QtArgParse(ArgParse& ap, QWidget* parent = nullptr);
    virtual ~QtArgParse() {}
};


OIIO_NAMESPACE_END
