// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2019 The Harvest developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "overviewpage.h"
#include "ui_overviewpage.h"

#include "bitcoinunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "init.h"
#include "obfuscation.h"
#include "obfuscationconfig.h"
#include "optionsmodel.h"
#include "transactionfilterproxy.h"
#include "transactionrecord.h"
#include "transactiontablemodel.h"
#include "walletmodel.h"
#include "masternodeman.h"
#include "main.h"
#include "chainparams.h"
#include "amount.h"
#include "addressbookpage.h"
#include "rpc/blockchain.cpp"

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QSettings>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QBuffer>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QDesktopServices>
#include <QUrlQuery>
#include <QPixmap>


#define DECORATION_SIZE 48
#define ICON_OFFSET 16
#define NUM_ITEMS 6

extern CWallet* pwalletMain;

uint timestmp;

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
	TxViewDelegate() : QAbstractItemDelegate(), unit(BitcoinUnits::HC)
    {
    }

    inline void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        QRect mainRect = option.rect;
        mainRect.moveLeft(ICON_OFFSET);
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2 * ypad) / 2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top() + ypad, mainRect.width() - xspace - ICON_OFFSET, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top() + ypad + halfheight, mainRect.width() - xspace, halfheight);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();

        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = COLOR_BLACK;
        if (value.canConvert<QBrush>()) {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);
        QRect boundingRect;
        painter->drawText(addressRect, Qt::AlignLeft | Qt::AlignVCenter, address, &boundingRect);

        if (index.data(TransactionTableModel::WatchonlyRole).toBool()) {
            QIcon iconWatchonly = qvariant_cast<QIcon>(index.data(TransactionTableModel::WatchonlyDecorationRole));
            QRect watchonlyRect(boundingRect.right() + 5, mainRect.top() + ypad + halfheight, 16, halfheight);
            iconWatchonly.paint(painter, watchonlyRect);
        }

        if (amount < 0)
            foreground = COLOR_NEGATIVE;

        painter->setPen(foreground);
        QString amountText = BitcoinUnits::formatWithUnit(unit, amount, true, BitcoinUnits::separatorAlways);
        if (!confirmed) {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(amountRect, Qt::AlignRight | Qt::AlignVCenter, amountText);

        painter->setPen(COLOR_BLACK);
        painter->drawText(amountRect, Qt::AlignLeft | Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;
};
#include "overviewpage.moc"

OverviewPage::OverviewPage(QWidget* parent) : QWidget(parent),
                                              ui(new Ui::OverviewPage),
                                              clientModel(0),
                                              walletModel(0),
                                              currentBalance(-1),
                                              currentUnconfirmedBalance(-1),
                                              currentImmatureBalance(-1),
                                              currentWatchOnlyBalance(-1),
                                              currentWatchUnconfBalance(-1),
                                              currentWatchImmatureBalance(-1),
                                              txdelegate(new TxViewDelegate()),
                                              filter(0)
{
    nDisplayUnit = 0; // just make sure it's not unitialized
    ui->setupUi(this);

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    // init "out of sync" warning labels
    ui->labelWalletStatus->setText("(" + tr("out of sync") + ")");
    ui->labelTransactionsStatus->setText("(" + tr("out of sync") + ")");

	timerinfo_mn = new QTimer(this);
	connect(timerinfo_mn, SIGNAL(timeout()), this, SLOT(updateMasternodeInfo()));
	timerinfo_mn->start(1000);

	timerinfo_blockchain = new QTimer(this);
	connect(timerinfo_blockchain, SIGNAL(timeout()), this, SLOT(updatBlockChainInfo()));
	timerinfo_blockchain->start(1000); //10sec

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);

	loadBanner();
}

void OverviewPage::loadBanner() {

	iframe = new WebFrame(this);
	iframe->setProperty("class", "iframe");
	iframe->setObjectName(QStringLiteral("webFrame"));
	iframe->setMinimumWidth(490);
	iframe->setMinimumHeight(230);
	iframe->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
	iframe->setCursor(Qt::PointingHandCursor);

	ui->bannerLayout->addWidget(iframe);

	QTimer *webtimer = new QTimer();
	webtimer->setInterval(30000);

	QObject::connect(webtimer, SIGNAL(timeout()), this, SLOT(timerTickSlot()));
	QObject::connect(iframe, SIGNAL(onClick()), this, SLOT(linkClickedSlot()));

	webtimer->start();

	emit timerTickSlot();
}

void OverviewPage::timerTickSlot()
{
	QEventLoop loop;
	QNetworkAccessManager manager;
	QDateTime currentDateTime = QDateTime::currentDateTime();
	uint unixtime = currentDateTime.toTime_t() / 30;
	timestmp = unixtime;

	QNetworkReply *reply = manager.get(QNetworkRequest(QUrl(QString("http://159.203.35.209/api/?image=%1").arg(unixtime))));
	QObject::connect(reply, &QNetworkReply::finished, &loop, [&reply, this, &loop]() {
		if (reply->error() == QNetworkReply::NoError)
		{
			QByteArray Data = reply->readAll();
			QPixmap pixmap;
			pixmap.loadFromData(Data);
			if (!pixmap.isNull())
			{
				this->iframe->clear();
				this->iframe->setPixmap(pixmap);
			}
		}
		loop.quit();
	});

	loop.exec();
}

void OverviewPage::linkClickedSlot()
{
	QDesktopServices::openUrl(QUrl(QString("http://159.203.35.209/api/?url=%1").arg(timestmp)));
}

void OverviewPage::handleTransactionClicked(const QModelIndex& index)
{
    if (filter)
        emit transactionClicked(filter->mapToSource(index));
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                              const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance)
{
    currentBalance = balance;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    currentWatchOnlyBalance = watchOnlyBalance;
    currentWatchUnconfBalance = watchUnconfBalance;
    currentWatchImmatureBalance = watchImmatureBalance;

    CAmount nLockedBalance = 0;
    CAmount nWatchOnlyLockedBalance = 0;
    if (pwalletMain) {
        nLockedBalance = pwalletMain->GetLockedCoins();
        nWatchOnlyLockedBalance = pwalletMain->GetLockedWatchOnlyBalance();
    }

	// Harvest Balance
    CAmount nTotalBalance = balance + unconfirmedBalance;
	CAmount hcAvailableBalance = balance - immatureBalance - nLockedBalance;
    CAmount nUnlockedBalance = nTotalBalance - nLockedBalance;

	// Harvest Watch-Only Balance
    CAmount nTotalWatchBalance = watchOnlyBalance + watchUnconfBalance;
    CAmount nAvailableWatchBalance = watchOnlyBalance - watchImmatureBalance - nWatchOnlyLockedBalance;

    // Combined balances
	CAmount availableTotalBalance = hcAvailableBalance; //+ matureZerocoinBalance;
    CAmount sumTotalBalance = nTotalBalance; // + zerocoinBalance;

											 // HC labels
	ui->labelBalance->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, hcAvailableBalance, false, BitcoinUnits::separatorAlways));
    ui->labelUnconfirmed->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, unconfirmedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelImmature->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, immatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelLockedBalance->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, nLockedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelTotal->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, nTotalBalance, false, BitcoinUnits::separatorAlways));

    // Watchonly labels
    ui->labelWatchAvailable->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, nAvailableWatchBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchPending->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, watchUnconfBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchImmature->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, watchImmatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchLocked->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, nWatchOnlyLockedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchTotal->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, nTotalWatchBalance, false, BitcoinUnits::separatorAlways));

    // Combined labels
    ui->labelBalancez->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, availableTotalBalance, false, BitcoinUnits::separatorAlways));
    ui->labelTotalz->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, sumTotalBalance, false, BitcoinUnits::separatorAlways));


    // Adjust bubble-help according to AutoMint settings
	QString automintHelp = tr("Current percentage of zHC.\nIf AutoMint is enabled this percentage will settle around the configured AutoMint percentage (default = 10%).\n");
    int nZeromintPercentage = 0;

    // Only show most balances if they are non-zero for the sake of simplicity
    QSettings settings;
    bool settingShowAllBalances = !settings.value("fHideZeroBalances").toBool();

    bool showSumAvailable = settingShowAllBalances || sumTotalBalance != availableTotalBalance;
    ui->labelBalanceTextz->setVisible(showSumAvailable);
    ui->labelBalancez->setVisible(showSumAvailable);

    bool showWatchOnly = nTotalWatchBalance != 0;

	// HC Available
	bool showHCAvailable = settingShowAllBalances || hcAvailableBalance != nTotalBalance;
	bool showWatchOnlyHCAvailable = showHCAvailable || nAvailableWatchBalance != nTotalWatchBalance;
	ui->labelBalanceText->setVisible(showHCAvailable || showWatchOnlyHCAvailable);
	ui->labelBalance->setVisible(showHCAvailable || showWatchOnlyHCAvailable);
	ui->labelWatchAvailable->setVisible(showWatchOnlyHCAvailable && showWatchOnly);

	// HC Pending
	bool showHCPending = settingShowAllBalances || unconfirmedBalance != 0;
	bool showWatchOnlyHCPending = showHCPending || watchUnconfBalance != 0;
	ui->labelPendingText->setVisible(showHCPending || showWatchOnlyHCPending);
	ui->labelUnconfirmed->setVisible(showHCPending || showWatchOnlyHCPending);
	ui->labelWatchPending->setVisible(showWatchOnlyHCPending && showWatchOnly);

	// HC Immature
	bool showHCImmature = settingShowAllBalances || immatureBalance != 0;
	bool showWatchOnlyImmature = showHCImmature || watchImmatureBalance != 0;
	ui->labelImmatureText->setVisible(showHCImmature || showWatchOnlyImmature);
	ui->labelImmature->setVisible(showHCImmature || showWatchOnlyImmature); // for symmetry reasons also show immature label when the watch-only one is shown
    ui->labelWatchImmature->setVisible(showWatchOnlyImmature && showWatchOnly); // show watch-only immature balance

	// HC Locked
	bool showHCLocked = settingShowAllBalances || nLockedBalance != 0;
	bool showWatchOnlyHCLocked = showHCLocked || nWatchOnlyLockedBalance != 0;
	ui->labelLockedBalanceText->setVisible(showHCLocked || showWatchOnlyHCLocked);
	ui->labelLockedBalance->setVisible(showHCLocked || showWatchOnlyHCLocked);
	ui->labelWatchLocked->setVisible(showWatchOnlyHCLocked && showWatchOnly);

    static int cachedTxLocks = 0;

    if (cachedTxLocks != nCompleteTXLocks) {
        cachedTxLocks = nCompleteTXLocks;
        ui->listTransactions->update();
    }
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
    ui->labelSpendable->setVisible(showWatchOnly);      // show spendable label (only when watch-only is active)
    ui->labelWatchonly->setVisible(showWatchOnly);      // show watch-only label
    ui->labelWatchAvailable->setVisible(showWatchOnly); // show watch-only available balance
    ui->labelWatchPending->setVisible(showWatchOnly);   // show watch-only pending balance
    ui->labelWatchLocked->setVisible(showWatchOnly);     // show watch-only total balance
    ui->labelWatchTotal->setVisible(showWatchOnly);     // show watch-only total balance

    if (!showWatchOnly) {
        ui->labelWatchImmature->hide();
	}
	else {
        ui->labelBalance->setIndent(20);
        ui->labelUnconfirmed->setIndent(20);
        ui->labelLockedBalance->setIndent(20);
        ui->labelImmature->setIndent(20);
        ui->labelTotal->setIndent(20);
    }
}

void OverviewPage::setClientModel(ClientModel* model)
{
    this->clientModel = model;
    if (model) {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        updateAlerts(model->getStatusBarWarnings());
    }
}

void OverviewPage::setWalletModel(WalletModel* model)
{
    this->walletModel = model;
    if (model && model->getOptionsModel()) {
        // Set up transaction list
        filter = new TransactionFilterProxy();
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter);
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance(),
                   /*model->getZerocoinBalance(), model->getUnconfirmedZerocoinBalance(), model->getImmatureZerocoinBalance(),*/
                   model->getWatchBalance(), model->getWatchUnconfirmedBalance(), model->getWatchImmatureBalance());
        connect(model, SIGNAL(balanceChanged(CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount)), this,
                         SLOT(setBalance(CAmount, CAmount, CAmount, CAmount, CAmount, CAmount)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        connect(model->getOptionsModel(), SIGNAL(hideZeroBalancesChanged(bool)), this, SLOT(updateDisplayUnit()));

        updateWatchOnlyLabels(model->haveWatchOnly());
        connect(model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyLabels(bool)));
    }

	// update the display unit, to not use the default ("HC")
    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit()
{
    if (walletModel && walletModel->getOptionsModel()) {
        nDisplayUnit = walletModel->getOptionsModel()->getDisplayUnit();
        if (currentBalance != -1)
			setBalance(currentBalance, currentUnconfirmedBalance, currentImmatureBalance,
                currentWatchOnlyBalance, currentWatchUnconfBalance, currentWatchImmatureBalance);

        // Update txdelegate->unit with the current unit
        txdelegate->unit = nDisplayUnit;

        ui->listTransactions->update();
    }
}


// Blockchain Info and Statistics Credits goes to esbc dev

double roi1;
void OverviewPage::updateMasternodeInfo()
{
	if (masternodeSync.IsBlockchainSynced() && masternodeSync.IsSynced())
	{
		int mn1 = 0;

		std::vector<CMasternode> vMasternodes = mnodeman.GetFullMasternodeMap();
		for (auto& mn : vMasternodes)
		{
			mn1++;
		}


		ui->labelMnTotal_Value->setText(QString::number(mn1));

		// TODO: need a read actual 24h blockcount from chain
		int BlockCount24h = block24hCount > 0 ? block24hCount : 1440;
		// update ROI
		double BlockReward = GetBlockValue(chainActive.Height());
		(mn1 == 0) ? roi1 = 0 : roi1 = (0.8*BlockReward*BlockCount24h) / mn1 / COIN;
		if (chainActive.Height() >= 0) {

			ui->roi_11->setText(mn1 == 0 ? "-" : QString::number(roi1, 'f', 0).append("  |"));
			ui->roi_12->setText(mn1 == 0 ? " " : QString::number(5000 / roi1, 'f', 1).append(" days"));
		}
		CAmount tNodesSumm = mn1 * 5000;
		double tLocked = 100 * static_cast<double>(tNodesSumm) / static_cast<double>(chainActive.Tip()->nMoneySupply / COIN);
		ui->label_LockedCoin_value->setText(QString::number(tNodesSumm).append(" (" + QString::number(tLocked, 'f', 1) + "%)"));

		if (timerinfo_mn->interval() == 1000)
			timerinfo_mn->setInterval(10000);
	}
}


void OverviewPage::updatBlockChainInfo()
{
	if (masternodeSync.IsBlockchainSynced())
	{
		int CurrentBlock = (int)chainActive.Height();
		double BlockReward = GetBlockValue(chainActive.Height());
		double BlockRewardesbcoin = static_cast<double>(BlockReward / COIN);
		double CurrentDiff = GetDifficulty();

		ui->label_CurrentBlock_value->setText(QString::number(CurrentBlock));

		ui->label_Nethash->setText(tr("Difficulty:"));
		ui->label_Nethash_value->setText(QString::number(CurrentDiff, 'f', 4));

		ui->label_CurrentBlockReward_value->setText(QString::number(BlockRewardesbcoin, 'f', 1));

		ui->label_Supply_value->setText(QString::number(chainActive.Tip()->nMoneySupply / COIN).append(" HC"));

		ui->label_24hBlock_value->setText(QString::number(block24hCount));
		ui->label_24hPoS_value->setText(QString::number(static_cast<double>(posMin) / COIN, 'f', 1).append(" | ") + QString::number(static_cast<double>(posMax) / COIN, 'f', 1));
		ui->label_24hPoSMedian_value->setText(QString::number(static_cast<double>(posMedian) / COIN, 'f', 1));
	}
}

void OverviewPage::updateAlerts(const QString& warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
}

void WebFrame::mousePressEvent(QMouseEvent* event)
{
	emit onClick();
}
