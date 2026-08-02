/****************************************************************************
** Meta object code from reading C++ file 'transit_workers.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/gui/transit_workers.h"
#include <QtNetwork/QSslError>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'transit_workers.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.1. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN8dracoved12SearchWorkerE_t {};
} // unnamed namespace

template <> constexpr inline auto dracoved::SearchWorker::qt_create_metaobjectdata<qt_meta_tag_ZN8dracoved12SearchWorkerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "dracoved::SearchWorker",
        "progressUpdate",
        "",
        "percent",
        "status",
        "resultFound",
        "dracoved::MainWindow::TransitSearchResult",
        "result",
        "finished",
        "cancelled",
        "error",
        "run",
        "cancel"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'progressUpdate'
        QtMocHelpers::SignalData<void(int, const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 }, { QMetaType::QString, 4 },
        }}),
        // Signal 'resultFound'
        QtMocHelpers::SignalData<void(const dracoved::MainWindow::TransitSearchResult &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 6, 7 },
        }}),
        // Signal 'finished'
        QtMocHelpers::SignalData<void(bool, const QString &)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 9 }, { QMetaType::QString, 10 },
        }}),
        // Slot 'run'
        QtMocHelpers::SlotData<void()>(11, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'cancel'
        QtMocHelpers::SlotData<void()>(12, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<SearchWorker, qt_meta_tag_ZN8dracoved12SearchWorkerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject dracoved::SearchWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8dracoved12SearchWorkerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8dracoved12SearchWorkerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN8dracoved12SearchWorkerE_t>.metaTypes,
    nullptr
} };

void dracoved::SearchWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<SearchWorker *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->progressUpdate((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 1: _t->resultFound((*reinterpret_cast<std::add_pointer_t<dracoved::MainWindow::TransitSearchResult>>(_a[1]))); break;
        case 2: _t->finished((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 3: _t->run(); break;
        case 4: _t->cancel(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 1:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< dracoved::MainWindow::TransitSearchResult >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (SearchWorker::*)(int , const QString & )>(_a, &SearchWorker::progressUpdate, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (SearchWorker::*)(const dracoved::MainWindow::TransitSearchResult & )>(_a, &SearchWorker::resultFound, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (SearchWorker::*)(bool , const QString & )>(_a, &SearchWorker::finished, 2))
            return;
    }
}

const QMetaObject *dracoved::SearchWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *dracoved::SearchWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8dracoved12SearchWorkerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int dracoved::SearchWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void dracoved::SearchWorker::progressUpdate(int _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}

// SIGNAL 1
void dracoved::SearchWorker::resultFound(const dracoved::MainWindow::TransitSearchResult & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void dracoved::SearchWorker::finished(bool _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1, _t2);
}
namespace {
struct qt_meta_tag_ZN8dracoved14CalendarWorkerE_t {};
} // unnamed namespace

template <> constexpr inline auto dracoved::CalendarWorker::qt_create_metaobjectdata<qt_meta_tag_ZN8dracoved14CalendarWorkerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "dracoved::CalendarWorker",
        "progressUpdate",
        "",
        "percent",
        "status",
        "finished",
        "cancelled",
        "error",
        "run",
        "cancel"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'progressUpdate'
        QtMocHelpers::SignalData<void(int, const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 }, { QMetaType::QString, 4 },
        }}),
        // Signal 'finished'
        QtMocHelpers::SignalData<void(bool, const QString &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 6 }, { QMetaType::QString, 7 },
        }}),
        // Slot 'run'
        QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'cancel'
        QtMocHelpers::SlotData<void()>(9, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CalendarWorker, qt_meta_tag_ZN8dracoved14CalendarWorkerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject dracoved::CalendarWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8dracoved14CalendarWorkerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8dracoved14CalendarWorkerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN8dracoved14CalendarWorkerE_t>.metaTypes,
    nullptr
} };

void dracoved::CalendarWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CalendarWorker *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->progressUpdate((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 1: _t->finished((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 2: _t->run(); break;
        case 3: _t->cancel(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (CalendarWorker::*)(int , const QString & )>(_a, &CalendarWorker::progressUpdate, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (CalendarWorker::*)(bool , const QString & )>(_a, &CalendarWorker::finished, 1))
            return;
    }
}

const QMetaObject *dracoved::CalendarWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *dracoved::CalendarWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8dracoved14CalendarWorkerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int dracoved::CalendarWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 4)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 4)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 4;
    }
    return _id;
}

// SIGNAL 0
void dracoved::CalendarWorker::progressUpdate(int _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}

// SIGNAL 1
void dracoved::CalendarWorker::finished(bool _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2);
}
namespace {
struct qt_meta_tag_ZN8dracoved17ConjunctionWorkerE_t {};
} // unnamed namespace

template <> constexpr inline auto dracoved::ConjunctionWorker::qt_create_metaobjectdata<qt_meta_tag_ZN8dracoved17ConjunctionWorkerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "dracoved::ConjunctionWorker",
        "progressUpdate",
        "",
        "percent",
        "status",
        "finished",
        "cancelled",
        "error",
        "run",
        "cancel"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'progressUpdate'
        QtMocHelpers::SignalData<void(int, const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 }, { QMetaType::QString, 4 },
        }}),
        // Signal 'finished'
        QtMocHelpers::SignalData<void(bool, const QString &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 6 }, { QMetaType::QString, 7 },
        }}),
        // Slot 'run'
        QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'cancel'
        QtMocHelpers::SlotData<void()>(9, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<ConjunctionWorker, qt_meta_tag_ZN8dracoved17ConjunctionWorkerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject dracoved::ConjunctionWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8dracoved17ConjunctionWorkerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8dracoved17ConjunctionWorkerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN8dracoved17ConjunctionWorkerE_t>.metaTypes,
    nullptr
} };

void dracoved::ConjunctionWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<ConjunctionWorker *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->progressUpdate((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 1: _t->finished((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 2: _t->run(); break;
        case 3: _t->cancel(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (ConjunctionWorker::*)(int , const QString & )>(_a, &ConjunctionWorker::progressUpdate, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (ConjunctionWorker::*)(bool , const QString & )>(_a, &ConjunctionWorker::finished, 1))
            return;
    }
}

const QMetaObject *dracoved::ConjunctionWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *dracoved::ConjunctionWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8dracoved17ConjunctionWorkerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int dracoved::ConjunctionWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 4)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 4)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 4;
    }
    return _id;
}

// SIGNAL 0
void dracoved::ConjunctionWorker::progressUpdate(int _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}

// SIGNAL 1
void dracoved::ConjunctionWorker::finished(bool _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2);
}
namespace {
struct qt_meta_tag_ZN8dracoved14LunationWorkerE_t {};
} // unnamed namespace

template <> constexpr inline auto dracoved::LunationWorker::qt_create_metaobjectdata<qt_meta_tag_ZN8dracoved14LunationWorkerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "dracoved::LunationWorker",
        "progressUpdate",
        "",
        "percent",
        "status",
        "finished",
        "cancelled",
        "error",
        "run",
        "cancel"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'progressUpdate'
        QtMocHelpers::SignalData<void(int, const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 }, { QMetaType::QString, 4 },
        }}),
        // Signal 'finished'
        QtMocHelpers::SignalData<void(bool, const QString &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 6 }, { QMetaType::QString, 7 },
        }}),
        // Slot 'run'
        QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'cancel'
        QtMocHelpers::SlotData<void()>(9, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<LunationWorker, qt_meta_tag_ZN8dracoved14LunationWorkerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject dracoved::LunationWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8dracoved14LunationWorkerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8dracoved14LunationWorkerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN8dracoved14LunationWorkerE_t>.metaTypes,
    nullptr
} };

void dracoved::LunationWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<LunationWorker *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->progressUpdate((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 1: _t->finished((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 2: _t->run(); break;
        case 3: _t->cancel(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (LunationWorker::*)(int , const QString & )>(_a, &LunationWorker::progressUpdate, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (LunationWorker::*)(bool , const QString & )>(_a, &LunationWorker::finished, 1))
            return;
    }
}

const QMetaObject *dracoved::LunationWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *dracoved::LunationWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8dracoved14LunationWorkerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int dracoved::LunationWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 4)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 4)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 4;
    }
    return _id;
}

// SIGNAL 0
void dracoved::LunationWorker::progressUpdate(int _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}

// SIGNAL 1
void dracoved::LunationWorker::finished(bool _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2);
}
namespace {
struct qt_meta_tag_ZN8dracoved23TransitAspectPeakWorkerE_t {};
} // unnamed namespace

template <> constexpr inline auto dracoved::TransitAspectPeakWorker::qt_create_metaobjectdata<qt_meta_tag_ZN8dracoved23TransitAspectPeakWorkerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "dracoved::TransitAspectPeakWorker",
        "progress",
        "",
        "done",
        "total",
        "error",
        "message",
        "finished",
        "run",
        "cancel"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'progress'
        QtMocHelpers::SignalData<void(int, int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 }, { QMetaType::Int, 4 },
        }}),
        // Signal 'error'
        QtMocHelpers::SignalData<void(const QString &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 6 },
        }}),
        // Signal 'finished'
        QtMocHelpers::SignalData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'run'
        QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'cancel'
        QtMocHelpers::SlotData<void()>(9, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<TransitAspectPeakWorker, qt_meta_tag_ZN8dracoved23TransitAspectPeakWorkerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject dracoved::TransitAspectPeakWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8dracoved23TransitAspectPeakWorkerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8dracoved23TransitAspectPeakWorkerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN8dracoved23TransitAspectPeakWorkerE_t>.metaTypes,
    nullptr
} };

void dracoved::TransitAspectPeakWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<TransitAspectPeakWorker *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->progress((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 1: _t->error((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->finished(); break;
        case 3: _t->run(); break;
        case 4: _t->cancel(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (TransitAspectPeakWorker::*)(int , int )>(_a, &TransitAspectPeakWorker::progress, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (TransitAspectPeakWorker::*)(const QString & )>(_a, &TransitAspectPeakWorker::error, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (TransitAspectPeakWorker::*)()>(_a, &TransitAspectPeakWorker::finished, 2))
            return;
    }
}

const QMetaObject *dracoved::TransitAspectPeakWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *dracoved::TransitAspectPeakWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8dracoved23TransitAspectPeakWorkerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int dracoved::TransitAspectPeakWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void dracoved::TransitAspectPeakWorker::progress(int _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}

// SIGNAL 1
void dracoved::TransitAspectPeakWorker::error(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void dracoved::TransitAspectPeakWorker::finished()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}
namespace {
struct qt_meta_tag_ZN8dracoved17TransitScanWorkerE_t {};
} // unnamed namespace

template <> constexpr inline auto dracoved::TransitScanWorker::qt_create_metaobjectdata<qt_meta_tag_ZN8dracoved17TransitScanWorkerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "dracoved::TransitScanWorker",
        "progress",
        "",
        "done",
        "total",
        "error",
        "message",
        "finished",
        "run",
        "cancel"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'progress'
        QtMocHelpers::SignalData<void(int, int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 }, { QMetaType::Int, 4 },
        }}),
        // Signal 'error'
        QtMocHelpers::SignalData<void(const QString &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 6 },
        }}),
        // Signal 'finished'
        QtMocHelpers::SignalData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'run'
        QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'cancel'
        QtMocHelpers::SlotData<void()>(9, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<TransitScanWorker, qt_meta_tag_ZN8dracoved17TransitScanWorkerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject dracoved::TransitScanWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8dracoved17TransitScanWorkerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8dracoved17TransitScanWorkerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN8dracoved17TransitScanWorkerE_t>.metaTypes,
    nullptr
} };

void dracoved::TransitScanWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<TransitScanWorker *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->progress((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 1: _t->error((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->finished(); break;
        case 3: _t->run(); break;
        case 4: _t->cancel(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (TransitScanWorker::*)(int , int )>(_a, &TransitScanWorker::progress, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (TransitScanWorker::*)(const QString & )>(_a, &TransitScanWorker::error, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (TransitScanWorker::*)()>(_a, &TransitScanWorker::finished, 2))
            return;
    }
}

const QMetaObject *dracoved::TransitScanWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *dracoved::TransitScanWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8dracoved17TransitScanWorkerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int dracoved::TransitScanWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void dracoved::TransitScanWorker::progress(int _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}

// SIGNAL 1
void dracoved::TransitScanWorker::error(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void dracoved::TransitScanWorker::finished()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}
QT_WARNING_POP
