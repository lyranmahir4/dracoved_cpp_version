#pragma once

#include "return_finder_types.h"

#include <QObject>

#include <atomic>

namespace dracoved {

class ReturnFinderWorker : public QObject {
    Q_OBJECT

public:
    explicit ReturnFinderWorker(const ReturnFinderQuery& query);

    void cancel();

public slots:
    void run();

signals:
    void progress(int completed, int total, const QString& status);
    void finished(const QVector<dracoved::ReturnFinderResult>& results,
                  const dracoved::ReturnFinderRunSummary& summary,
                  const QString& error);

private:
    ReturnFinderQuery query_;
    std::atomic<bool> cancelled_{false};
};

}  // namespace dracoved

