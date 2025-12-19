// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2022-2026 The Radiant developers
// Copyright (c) 2017-2021 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/bitcoinaddressvalidator.h>

#include <config.h>
#include <key_io.h>

#ifdef ENABLE_WALLET
#include <qt/legacyaddressconvertdialog.h>
#include <qt/legacyaddressdialog.h>
#endif // ENABLE_WALLET

#include <QSettings>
#include <QWidget>

/* Base58 characters are:
     "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"

  This is:
  - All numbers except for '0'
  - All upper-case letters except for 'I' and 'O'
  - All lower-case letters except for 'l'
*/
BitcoinAddressEntryValidator::BitcoinAddressEntryValidator(
    QObject *parent)
    : QValidator(parent) {}

QValidator::State BitcoinAddressEntryValidator::validate(QString &input, [[maybe_unused]] int &pos) const {

    // Empty address is "intermediate" input
    if (input.isEmpty()) {
        return QValidator::Intermediate;
    }

    // Correction
    for (int idx = 0; idx < input.size();) {
        bool removeChar = false;
        QChar ch = input.at(idx);
        // Corrections made are very conservative on purpose, to avoid
        // users unexpectedly getting away with typos that would normally
        // be detected, and thus sending to the wrong address.
        switch (ch.unicode()) {
            // Qt categorizes these as "Other_Format" not "Separator_Space"
            case 0x200B: // ZERO WIDTH SPACE
            case 0xFEFF: // ZERO WIDTH NO-BREAK SPACE
                removeChar = true;
                break;
            default:
                break;
        }

        // Remove whitespace
        if (ch.isSpace()) {
            removeChar = true;
        }

        // To next character
        if (removeChar) {
            input.remove(idx, 1);
        } else {
            ++idx;
        }
    }

    // Validation
    for (int idx = 0; idx < input.size(); ++idx) {
        int ch = input.at(idx).unicode();

        if ((ch < '0' || ch > '9') && (ch < 'a' || ch > 'z') && (ch < 'A' || ch > 'Z')) {
            // Only alphanumeric allowed.
            return QValidator::Invalid;
        }
    }

    return QValidator::Acceptable;
}

BitcoinAddressCheckValidator::BitcoinAddressCheckValidator(QWidget *parent)
    : QValidator(parent) {}

QValidator::State BitcoinAddressCheckValidator::validate(QString &input, [[maybe_unused]] int &pos) const {
    // If the address is otherwise valid, do some fix-up immediately.
    CTxDestination destination = DecodeDestination(input.toStdString(), GetConfig().GetChainParams());
    if (IsValidDestination(destination)) {
        // Normalize address notation
        input = QString::fromStdString(EncodeDestination(destination, GetConfig()));
        return QValidator::Acceptable;
    }
    return QValidator::Invalid;
}


void BitcoinAddressCheckValidator::fixup(QString &input) const /*override*/ {
    CTxDestination destination = DecodeDestination(input.toStdString(), GetConfig().GetChainParams());
    if (IsValidDestination(destination)) {
        // We have a valid address - normalize notation
        QString normalizedInput = QString::fromStdString(EncodeDestination(destination, GetConfig()));
        input = normalizedInput;
    }
}

bool BitcoinAddressCheckValidator::GetLegacyAddressUseAuth(const CTxDestination &destination) const {
#ifdef ENABLE_WALLET
    QSettings settings;
    LegacyAddressType addressType = LegacyAddressType::P2PKH;
    bool allowed = settings.value("fAllowLegacyP2PKH").toBool();
    if (allowed) {
        // Give warning and allow the user to proceed
        LegacyAddressWarnDialog dlg(parentWidget());
        dlg.SetAddressType(addressType);
        if (!dlg.exec()) {
            allowed = false;
        }
    } else {
        // Give warning and deny permission to proceed
        LegacyAddressStopDialog dlg(parentWidget());
        dlg.SetAddressType(addressType);
        dlg.exec();
        allowed = false;
    }
    return allowed;
#else
    return true;
#endif //ENABLE_WALLET
}

bool BitcoinAddressCheckValidator::GetLegacyAddressConversionAuth(const QString &original, const QString &normalized) const {
#ifdef ENABLE_WALLET
    LegacyAddressConvertDialog dlg(parentWidget());
    dlg.SetAddresses(original, normalized);
    dlg.adjustSize();
    return dlg.exec();
#else
    return true;
#endif //ENABLE_WALLET
}

QWidget* BitcoinAddressCheckValidator::parentWidget() const {
    return qobject_cast<QWidget *>(parent());
}
