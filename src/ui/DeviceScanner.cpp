// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/DeviceScanner.hpp"

#include <vloom/engine/io/AudioDevice.hpp>

#include <QThread>

#include <exception>
#include <memory>
#include <utility>

namespace vloomdaw {

DeviceScanner::DeviceScanner(QObject* parent)
    : QObject(parent)
{
    worker_ = new QThread(this);
    worker_->setObjectName(QStringLiteral("DeviceScanner"));
    lister_ = new QObject; // no parent: it moves to the worker thread
    lister_->moveToThread(worker_);
    connect(worker_, &QThread::finished, lister_, &QObject::deleteLater);
    worker_->start(QThread::LowPriority);
}

DeviceScanner::~DeviceScanner()
{
    worker_->quit();
    worker_->wait(); // nothing reaches this object afterwards
}

void DeviceScanner::scan()
{
    if (busy_)
        return;
    busy_ = true;
    QMetaObject::invokeMethod(
        lister_,
        [this] {
            // Worker thread. `this` outlives it: the destructor joins the thread.
            auto names = std::make_shared<std::vector<std::string>>();
            try {
                vloom::AudioDevice device;
                for (const auto& info : device.listOutputDevices())
                    names->push_back(info.name);
            } catch (const std::exception&) {
                names->clear(); // nothing found this time; the next scan tries again
            }
            QMetaObject::invokeMethod(
                this,
                [this, names] {
                    names_ = std::move(*names);
                    busy_ = false;
                    emit scanned();
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

} // namespace vloomdaw
