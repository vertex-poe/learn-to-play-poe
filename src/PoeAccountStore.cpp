#include "PoeAccountStore.h"

#include <keychain.h>

PoeAccountStore::PoeAccountStore(QObject *parent)
    : QObject(parent)
{}

void PoeAccountStore::readSession()
{
    auto *job = new QKeychain::ReadPasswordJob(QLatin1String(kService), this);
    job->setAutoDelete(true);
    job->setKey(QLatin1String(kKey));
    connect(job, &QKeychain::Job::finished, this, [this, job]() {
        emit sessionRead(job->error() == QKeychain::NoError ? job->textData() : QString{});
    });
    job->start();
}

void PoeAccountStore::writeSession(const QString &poesessid)
{
    auto *job = new QKeychain::WritePasswordJob(QLatin1String(kService), this);
    job->setAutoDelete(true);
    job->setKey(QLatin1String(kKey));
    job->setTextData(poesessid);
    connect(job, &QKeychain::Job::finished, this, [this, job]() {
        emit sessionWritten(job->error() == QKeychain::NoError);
    });
    job->start();
}

void PoeAccountStore::deleteSession()
{
    auto *job = new QKeychain::DeletePasswordJob(QLatin1String(kService), this);
    job->setAutoDelete(true);
    job->setKey(QLatin1String(kKey));
    connect(job, &QKeychain::Job::finished, this, [this, job]() {
        emit sessionDeleted(job->error() == QKeychain::NoError);
    });
    job->start();
}
