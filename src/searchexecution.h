// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractItemModel>
#include <QMetaObject>

// Public invokable methods on the installed Milou/KRunner model. Keep native
// runner execution in the production host; tests use a model that records calls.
inline bool executeSearchModel(QAbstractItemModel *model, const QModelIndex &index, int action)
{
    bool accepted = false;
    if (action < 0)
        QMetaObject::invokeMethod(model, "run", Qt::DirectConnection, Q_RETURN_ARG(bool, accepted), Q_ARG(QModelIndex, index));
    else
        QMetaObject::invokeMethod(model, "runAction", Qt::DirectConnection, Q_RETURN_ARG(bool, accepted),
                                  Q_ARG(QModelIndex, index), Q_ARG(int, action));
    return accepted;
}
