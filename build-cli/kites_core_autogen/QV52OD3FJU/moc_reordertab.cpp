/****************************************************************************
** Meta object code from reading C++ file 'reordertab.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/ui/reorder_tab/reordertab.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'reordertab.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.3. It"
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
struct qt_meta_tag_ZN5Kites10ReorderTabE_t {};
} // unnamed namespace

template <> constexpr inline auto Kites::ReorderTab::qt_create_metaobjectdata<qt_meta_tag_ZN5Kites10ReorderTabE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "Kites::ReorderTab",
        "exportRequested",
        "",
        "setReordered",
        "listing",
        "key",
        "cyclesBefore",
        "cyclesAfter",
        "stallsBefore",
        "stallsAfter",
        "weighted",
        "setMeasured",
        "reordered",
        "cycles",
        "stalls",
        "retired",
        "clear"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'exportRequested'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'setReordered'
        QtMocHelpers::SlotData<void(const QString &, const QString &, int, int, int, int, bool)>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 4 }, { QMetaType::QString, 5 }, { QMetaType::Int, 6 }, { QMetaType::Int, 7 },
            { QMetaType::Int, 8 }, { QMetaType::Int, 9 }, { QMetaType::Bool, 10 },
        }}),
        // Slot 'setMeasured'
        QtMocHelpers::SlotData<void(bool, unsigned , unsigned , unsigned )>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 12 }, { QMetaType::UInt, 13 }, { QMetaType::UInt, 14 }, { QMetaType::UInt, 15 },
        }}),
        // Slot 'clear'
        QtMocHelpers::SlotData<void()>(16, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<ReorderTab, qt_meta_tag_ZN5Kites10ReorderTabE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject Kites::ReorderTab::staticMetaObject = { {
    QMetaObject::SuperData::link<KitesTab::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN5Kites10ReorderTabE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN5Kites10ReorderTabE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN5Kites10ReorderTabE_t>.metaTypes,
    nullptr
} };

void Kites::ReorderTab::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<ReorderTab *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->exportRequested(); break;
        case 1: _t->setReordered((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[6])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[7]))); break;
        case 2: _t->setMeasured((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<uint>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<uint>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<uint>>(_a[4]))); break;
        case 3: _t->clear(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (ReorderTab::*)()>(_a, &ReorderTab::exportRequested, 0))
            return;
    }
}

const QMetaObject *Kites::ReorderTab::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Kites::ReorderTab::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN5Kites10ReorderTabE_t>.strings))
        return static_cast<void*>(this);
    return KitesTab::qt_metacast(_clname);
}

int Kites::ReorderTab::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = KitesTab::qt_metacall(_c, _id, _a);
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
void Kites::ReorderTab::exportRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}
QT_WARNING_POP
