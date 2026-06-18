#include <QtTest/QtTest>

class TestPlaceholder : public QObject
{
    Q_OBJECT
private slots:
    void sanity() { QVERIFY(1 + 1 == 2); }
};

QTEST_MAIN(TestPlaceholder)
#include "test_placeholder.moc"
