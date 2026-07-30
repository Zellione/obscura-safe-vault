#include <QtTest>
#include "transfer_controller.h"

class TransferControllerTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        controller_ = std::make_unique<TransferController>();
    }

    void cleanupTestCase()
    {
        controller_.reset();
    }

    void testDeleteItemsSignalsCompletion()
    {
        QList<quintptr> nodeIds;
        nodeIds.append(1);
        nodeIds.append(2);

        QSignalSpy finishedSpy(controller_.get(), &TransferController::finished);
        controller_->deleteItems(nodeIds);

        QVERIFY(finishedSpy.count() > 0);
    }

    void testTransferItemsCopy()
    {
        QList<quintptr> nodeIds;
        nodeIds.append(1);

        QSignalSpy finishedSpy(controller_.get(), &TransferController::finished);
        controller_->transferItems(nodeIds, true, "/vault", "/gallery");

        QVERIFY(finishedSpy.count() > 0);
    }

    void testTransferItemsMove()
    {
        QList<quintptr> nodeIds;
        nodeIds.append(1);

        QSignalSpy finishedSpy(controller_.get(), &TransferController::finished);
        controller_->transferItems(nodeIds, false, "/vault", "/gallery");

        QVERIFY(finishedSpy.count() > 0);
    }

    void testCombineGalleries()
    {
        QSignalSpy finishedSpy(controller_.get(), &TransferController::finished);
        controller_->combineGalleries(1, 2);

        QVERIFY(finishedSpy.count() > 0);
    }

    void testCompact()
    {
        QSignalSpy finishedSpy(controller_.get(), &TransferController::finished);
        controller_->compact();

        QVERIFY(finishedSpy.count() > 0);
    }

private:
    std::unique_ptr<TransferController> controller_;
};

QTEST_MAIN(TransferControllerTest)
#include "transfer_controller_test.moc"
