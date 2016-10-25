/* clang-format off */
/****************************************************************************
  Author:  Karelin Pavel (hkarel), hkarel@yandex.ru
  This header is defined macros of general purpose.
****************************************************************************/

#pragma once

#define DISABLE_DEFAULT_CONSTRUCT( ClassName ) \
    ClassName () = delete;                     \
    ClassName ( ClassName && ) = delete;       \
    ClassName ( const ClassName & ) = delete;

#define DISABLE_DEFAULT_COPY( ClassName )      \
    ClassName ( ClassName && ) = delete;       \
    ClassName ( const ClassName & ) = delete;  \
    ClassName & operator = ( ClassName && ) = delete; \
    ClassName & operator = ( const ClassName & ) = delete;

#define DISABLE_DEFAULT_FUNC( ClassName )      \
    ClassName () = delete;                     \
    ClassName ( ClassName && ) = delete;       \
    ClassName ( const ClassName & ) = delete;  \
    ClassName & operator = ( ClassName && ) = delete; \
    ClassName & operator = ( const ClassName & ) = delete;


/**
  The chk_connect macro is used to check the result returned by the function
  QObject::connect() in debug mode, it looks like on assert() function.
  However, in the release mode, unlike the assert() function - test expression
  is not removed.
  To use the macro in the code need include <assert.h>
*/
#ifndef NDEBUG
#define chk_connect(SOURCE_, SIGNAL_, DEST_, SLOT_, CONNECT_TYPE_) \
            Q_ASSERT(QObject::connect(SOURCE_, SIGNAL_, DEST_, SLOT_, CONNECT_TYPE_));

// It corresponds to the connection Qt::AutoConnection
#define chk_connect_a(SOURCE_, SIGNAL_, DEST_, SLOT_) \
            Q_ASSERT(QObject::connect(SOURCE_, SIGNAL_, DEST_, SLOT_, Qt::AutoConnection));

// It corresponds to the connection Qt::DirectConnection
#define chk_connect_d(SOURCE_, SIGNAL_, DEST_, SLOT_) \
            Q_ASSERT(QObject::connect(SOURCE_, SIGNAL_, DEST_, SLOT_, Qt::DirectConnection));

// It corresponds to the connection Qt::QueuedConnection
#define chk_connect_q(SOURCE_, SIGNAL_, DEST_, SLOT_) \
            Q_ASSERT(QObject::connect(SOURCE_, SIGNAL_, DEST_, SLOT_, Qt::QueuedConnection));

// It corresponds to the connection Qt::BlockingQueuedConnection
#define chk_connect_bq(SOURCE_, SIGNAL_, DEST_, SLOT_) \
            Q_ASSERT(QObject::connect(SOURCE_, SIGNAL_, DEST_, SLOT_, Qt::BlockingQueuedConnection));

#else //NDEBUG
#define chk_connect(SOURCE_, SIGNAL_, DEST_, SLOT_, CONNECT_TYPE_) \
            QObject::connect(SOURCE_, SIGNAL_, DEST_, SLOT_, CONNECT_TYPE_);

// It corresponds to the connection Qt::AutoConnection
#define chk_connect_a(SOURCE_, SIGNAL_, DEST_, SLOT_) \
            QObject::connect(SOURCE_, SIGNAL_, DEST_, SLOT_, Qt::AutoConnection);

// It corresponds to the connection Qt::DirectConnection
#define chk_connect_d(SOURCE_, SIGNAL_, DEST_, SLOT_) \
            QObject::connect(SOURCE_, SIGNAL_, DEST_, SLOT_, Qt::DirectConnection);

// It corresponds to the connection Qt::QueuedConnection
#define chk_connect_q(SOURCE_, SIGNAL_, DEST_, SLOT_) \
            QObject::connect(SOURCE_, SIGNAL_, DEST_, SLOT_, Qt::QueuedConnection);

// It corresponds to the connection Qt::BlockingQueuedConnection
#define chk_connect_bq(SOURCE_, SIGNAL_, DEST_, SLOT_) \
            QObject::connect(SOURCE_, SIGNAL_, DEST_, SLOT_, Qt::BlockingQueuedConnection);

#endif //NDEBUG
