/****************************************************************************
** Meta object code from reading C++ file 'ITunesPlayerWidget.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.12.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/ui/ITunesPlayerWidget.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'ITunesPlayerWidget.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.12.0. It"
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
struct qt_meta_tag_ZN10SeekSliderE_t {};
} // unnamed namespace

template <> constexpr inline auto SeekSlider::qt_create_metaobjectdata<qt_meta_tag_ZN10SeekSliderE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "SeekSlider"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<SeekSlider, qt_meta_tag_ZN10SeekSliderE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject SeekSlider::staticMetaObject = { {
    QMetaObject::SuperData::link<QSlider::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10SeekSliderE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10SeekSliderE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN10SeekSliderE_t>.metaTypes,
    nullptr
} };

void SeekSlider::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<SeekSlider *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *SeekSlider::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SeekSlider::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10SeekSliderE_t>.strings))
        return static_cast<void*>(this);
    return QSlider::qt_metacast(_clname);
}

int SeekSlider::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QSlider::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN18ITunesPlayerWidgetE_t {};
} // unnamed namespace

template <> constexpr inline auto ITunesPlayerWidget::qt_create_metaobjectdata<qt_meta_tag_ZN18ITunesPlayerWidgetE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ITunesPlayerWidget",
        "requestNextTrack",
        "",
        "requestPreviousTrack",
        "playbackStateChanged",
        "isPlaying",
        "onPlayPauseClicked",
        "onPrevClicked",
        "onNextClicked",
        "onPositionChanged",
        "position",
        "onDurationChanged",
        "duration",
        "onSliderMoved",
        "onSliderPressed",
        "onSliderReleased",
        "onVolumeChanged",
        "value",
        "onMediaStatusChanged",
        "QMediaPlayer::MediaStatus",
        "status",
        "onPlayerStateChanged",
        "QMediaPlayer::PlaybackState",
        "state"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'requestNextTrack'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'requestPreviousTrack'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'playbackStateChanged'
        QtMocHelpers::SignalData<void(bool)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 5 },
        }}),
        // Slot 'onPlayPauseClicked'
        QtMocHelpers::SlotData<void()>(6, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPrevClicked'
        QtMocHelpers::SlotData<void()>(7, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onNextClicked'
        QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPositionChanged'
        QtMocHelpers::SlotData<void(qint64)>(9, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::LongLong, 10 },
        }}),
        // Slot 'onDurationChanged'
        QtMocHelpers::SlotData<void(qint64)>(11, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::LongLong, 12 },
        }}),
        // Slot 'onSliderMoved'
        QtMocHelpers::SlotData<void(int)>(13, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 10 },
        }}),
        // Slot 'onSliderPressed'
        QtMocHelpers::SlotData<void()>(14, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onSliderReleased'
        QtMocHelpers::SlotData<void()>(15, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onVolumeChanged'
        QtMocHelpers::SlotData<void(int)>(16, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 17 },
        }}),
        // Slot 'onMediaStatusChanged'
        QtMocHelpers::SlotData<void(QMediaPlayer::MediaStatus)>(18, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 19, 20 },
        }}),
        // Slot 'onPlayerStateChanged'
        QtMocHelpers::SlotData<void(QMediaPlayer::PlaybackState)>(21, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 22, 23 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<ITunesPlayerWidget, qt_meta_tag_ZN18ITunesPlayerWidgetE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ITunesPlayerWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18ITunesPlayerWidgetE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18ITunesPlayerWidgetE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN18ITunesPlayerWidgetE_t>.metaTypes,
    nullptr
} };

void ITunesPlayerWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<ITunesPlayerWidget *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->requestNextTrack(); break;
        case 1: _t->requestPreviousTrack(); break;
        case 2: _t->playbackStateChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 3: _t->onPlayPauseClicked(); break;
        case 4: _t->onPrevClicked(); break;
        case 5: _t->onNextClicked(); break;
        case 6: _t->onPositionChanged((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1]))); break;
        case 7: _t->onDurationChanged((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1]))); break;
        case 8: _t->onSliderMoved((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 9: _t->onSliderPressed(); break;
        case 10: _t->onSliderReleased(); break;
        case 11: _t->onVolumeChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 12: _t->onMediaStatusChanged((*reinterpret_cast<std::add_pointer_t<QMediaPlayer::MediaStatus>>(_a[1]))); break;
        case 13: _t->onPlayerStateChanged((*reinterpret_cast<std::add_pointer_t<QMediaPlayer::PlaybackState>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (ITunesPlayerWidget::*)()>(_a, &ITunesPlayerWidget::requestNextTrack, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (ITunesPlayerWidget::*)()>(_a, &ITunesPlayerWidget::requestPreviousTrack, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (ITunesPlayerWidget::*)(bool )>(_a, &ITunesPlayerWidget::playbackStateChanged, 2))
            return;
    }
}

const QMetaObject *ITunesPlayerWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ITunesPlayerWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18ITunesPlayerWidgetE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int ITunesPlayerWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 14)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 14;
    }
    return _id;
}

// SIGNAL 0
void ITunesPlayerWidget::requestNextTrack()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void ITunesPlayerWidget::requestPreviousTrack()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void ITunesPlayerWidget::playbackStateChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}
QT_WARNING_POP
