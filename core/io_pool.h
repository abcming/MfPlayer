#pragma once
#include <QThreadPool>

// Dedicated I/O thread pool for filesystem operations (stat, QSettings write, dir enum).
// Separate from QThreadPool::globalInstance() so I/O-blocked threads don't starve CPU work.
QThreadPool &ioPool();
