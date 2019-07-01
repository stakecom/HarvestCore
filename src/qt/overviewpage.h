// Copyright (c) 2011-2013 The Bitcoin developers
// Copyright (c) 2017-2018 The PIVX developers
// Copyright (c) 2019 The Harvest developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_OVERVIEWPAGE_H
#define BITCOIN_QT_OVERVIEWPAGE_H

#include "amount.h"

#include <QWidget>
#include <QLabel>

class ClientModel;
class TransactionFilterProxy;
class TxViewDelegate;
class WalletModel;
class WebFrame;

extern uint timestmp;

namespace Ui
{
class OverviewPage;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Overview ("home") page widget */
class OverviewPage : public QWidget
{
    Q_OBJECT

public:
    explicit OverviewPage(QWidget* parent = 0);
    ~OverviewPage();

    void setClientModel(ClientModel* clientModel);
    void setWalletModel(WalletModel* walletModel);
    void showOutOfSyncWarning(bool fShow);

public slots:
    void setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance, 
                    const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance);

signals:
    void transactionClicked(const QModelIndex& index);

private:
    QTimer* timer;
	QTimer* timerinfo_mn;
	QTimer* timerinfo_blockchain;
    Ui::OverviewPage* ui;
    ClientModel* clientModel;
    WalletModel* walletModel;
	WebFrame* iframe;
    CAmount currentBalance;
    CAmount currentUnconfirmedBalance;
    CAmount currentImmatureBalance;
    CAmount currentWatchOnlyBalance;
    CAmount currentWatchUnconfBalance;
    CAmount currentWatchImmatureBalance;
    int nDisplayUnit;


    TxViewDelegate* txdelegate;
    TransactionFilterProxy* filter;

	void loadBanner();

signals:

	public slots :
	void linkClickedSlot();
	void timerTickSlot();

private slots:
    void updateDisplayUnit();
    void handleTransactionClicked(const QModelIndex& index);
    void updateAlerts(const QString& warnings);
    void updateWatchOnlyLabels(bool showWatchOnly);
    	void updatBlockChainInfo();
	void updateMasternodeInfo();
};

class WebFrame : public QLabel
{
	Q_OBJECT

		signals :
	void onClick();

public:
	/** So that it responds to left-button clicks */
	void mousePressEvent(QMouseEvent* event);

	using QLabel::QLabel;
};

#endif // BITCOIN_QT_OVERVIEWPAGE_H
