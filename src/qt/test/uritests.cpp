// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2022-2026 The Radiant developers
// Copyright (c) 2017-2019 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/test/uritests.h>

#include <chainparams.h>
#include <config.h>
#include <qt/guiutil.h>
#include <qt/walletmodel.h>

#include <QUrl>

void URITests::uriTestsCashAddr() {
    const auto params = CreateChainParams(CBaseChainParams::MAIN);

    SendCoinsRecipient rv;
    QUrl uri;
    QString scheme = QString("radiant");
    const QString base58 = "1BpEi6DfDAUFd7GtittLSdBeYJvcoaVggu";

    uri.setUrl(QString("%1:%2?req-dontexist=").arg(scheme).arg(base58));
    QVERIFY(!GUIUtil::parseBitcoinURI(scheme, uri, &rv));

    uri.setUrl(QString("%1:%2?dontexist=").arg(scheme).arg(base58));
    QVERIFY(GUIUtil::parseBitcoinURI(scheme, uri, &rv));
    QVERIFY(rv.address == base58);
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == Amount::zero());

    uri.setUrl(QString("%1:%2?label=Wikipedia Example Address").arg(scheme).arg(base58));
    QVERIFY(GUIUtil::parseBitcoinURI(scheme, uri, &rv));
    QVERIFY(rv.address == base58);
    QVERIFY(rv.label == QString("Wikipedia Example Address"));
    QVERIFY(rv.amount == Amount::zero());

    uri.setUrl(QString("%1:%2?amount=0.001").arg(scheme).arg(base58));
    QVERIFY(GUIUtil::parseBitcoinURI(scheme, uri, &rv));
    QVERIFY(rv.address == base58);
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 100000 * SATOSHI);

    uri.setUrl(QString("%1:%2?amount=1.001").arg(scheme).arg(base58));
    QVERIFY(GUIUtil::parseBitcoinURI(scheme, uri, &rv));
    QVERIFY(rv.address == base58);
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 100100000 * SATOSHI);

    uri.setUrl(QString("%1:%2?amount=100&label=Wikipedia Example").arg(scheme).arg(base58));
    QVERIFY(GUIUtil::parseBitcoinURI(scheme, uri, &rv));
    QVERIFY(rv.address == base58);
    QVERIFY(rv.amount == int64_t(10000000000) * SATOSHI);
    QVERIFY(rv.label == QString("Wikipedia Example"));

    uri.setUrl(QString("%1:%2?message=Wikipedia Example Address").arg(scheme).arg(base58));
    QVERIFY(GUIUtil::parseBitcoinURI(scheme, uri, &rv));
    QVERIFY(rv.address == base58);
    QVERIFY(rv.label == QString());

    QVERIFY(
        GUIUtil::parseBitcoinURI(scheme,
                                 QString("%1://%2?message=Wikipedia Example Address").arg(scheme).arg(base58),
                                 &rv));
    QVERIFY(rv.address == base58);
    QVERIFY(rv.label == QString());

    uri.setUrl(QString("%1:%2?req-message=Wikipedia Example Address").arg(scheme).arg(base58));
    QVERIFY(GUIUtil::parseBitcoinURI(scheme, uri, &rv));

    uri.setUrl(QString("%1:%2?amount=1,000&label=Wikipedia Example").arg(scheme).arg(base58));
    QVERIFY(!GUIUtil::parseBitcoinURI(scheme, uri, &rv));

    uri.setUrl(QString("%1:%2?amount=1,000.0&label=Wikipedia Example").arg(scheme).arg(base58));
    QVERIFY(!GUIUtil::parseBitcoinURI(scheme, uri, &rv));
}

void URITests::uriTestFormatURI() {
    const auto params = CreateChainParams(CBaseChainParams::MAIN);
    const QString scheme = QString("radiant");

    {
        SendCoinsRecipient r;
        r.address = "1BpEi6DfDAUFd7GtittLSdBeYJvcoaVggu";
        r.message = "test";
        QString uri = GUIUtil::formatBitcoinURI(*params, r);
        QVERIFY(uri == scheme + ":1BpEi6DfDAUFd7GtittLSdBeYJvcoaVggu?message=test");
    }

    {
        // Garbage goes through (address checksum is invalid)
        SendCoinsRecipient r;
        r.address = "175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W";
        r.message = "test";
        QString uri = GUIUtil::formatBitcoinURI(*params, r);
        QVERIFY(uri == "175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W?message=test");
    }

    {
        // Valid legacy base58 addresses are kept as base58.
        SendCoinsRecipient r;
        r.address = "12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX";
        r.message = "test";
        QString uri = GUIUtil::formatBitcoinURI(*params, r);
        QVERIFY(uri == scheme + ":12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX?message=test");
    }
}
