// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "proposalspage.h"
#include "ui_proposalspage.h"

#include "masternodelist.h"
#include "activemasternode.h"

#include "bitcoinunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "init.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "transactionfilterproxy.h"
#include "transactiontablemodel.h"
#include "utilitydialog.h"
#include "walletmodel.h"

#include "instantx.h"
//#include "darksendconfig.h"
#include "masternode-sync.h"
#include "governance.h"
#include "transport-curl.h"
#include "client.h"

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QSettings>
#include <QTimer>
#include <QObject>
#include <QStringListModel>
#include <QDesktopServices>
#include <QUrl>
#include <ctime>
#include <iomanip>
#include <QTreeWidget>
#include <QPushButton>
#include <QStringList>


#include "json.hpp"

using json = nlohmann::json;

#define ICON_OFFSET 16
#define DECORATION_SIZE 54
#define NUM_ITEMS 5
#define NUM_ITEMS_ADV 7

/*
class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    TxViewDelegate(const PlatformStyle *_platformStyle, QObject *parent=nullptr):
        QAbstractItemDelegate(parent), unit(BitcoinUnits::HTA),
        platformStyle(_platformStyle)
    {

    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {

    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;
    const PlatformStyle *platformStyle;

};
*/
#include "proposalspage.moc"

ProposalsPage::ProposalsPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ProposalsPage),
    clientModel(0),
    walletModel(0),
    currentBalance(-1),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    currentWatchOnlyBalance(-1),
    currentWatchUnconfBalance(-1),
    currentWatchImmatureBalance(-1),
//    txdelegate(new TxViewDelegate(platformStyle, this)),
    timer(nullptr)
{
    ui->setupUi(this);

    int columnNameWidth = 225;
    int columnDateWidth = 150;
    int columnIPFSCIDWidth = 233;
    int columnVoteRatioWidth = 200;
    int columnSummaryWidth = 225;

    //model = new QStringListModel(this);
    //QStringList List = listProposals();
    //model->setStringList(List);
    //ui->listProposals->setModel(model);

    ui->tableWidgetApprovedRecords->setColumnWidth(0, columnDateWidth);
    ui->tableWidgetApprovedRecords->setColumnWidth(1, columnNameWidth);
    ui->tableWidgetApprovedRecords->setColumnWidth(2, columnVoteRatioWidth);
    ui->tableWidgetApprovedRecords->setColumnWidth(3, columnIPFSCIDWidth);
    ui->tableWidgetApprovedRecords->setColumnWidth(4, columnSummaryWidth);


    ui->tableWidgetApprovedProposals->setColumnWidth(0, columnDateWidth);
    ui->tableWidgetApprovedProposals->setColumnWidth(1, columnNameWidth);
    ui->tableWidgetApprovedProposals->setColumnWidth(2, columnVoteRatioWidth);
    ui->tableWidgetApprovedProposals->setColumnWidth(3, columnIPFSCIDWidth);
    ui->tableWidgetApprovedProposals->setColumnWidth(4, columnSummaryWidth);

    ui->treeWidgetApprovedProposals->setColumnWidth(0, columnDateWidth);
    ui->treeWidgetApprovedProposals->setColumnWidth(1, columnNameWidth);
    ui->treeWidgetApprovedProposals->setColumnWidth(2, columnVoteRatioWidth);
    ui->treeWidgetApprovedProposals->setColumnWidth(3, columnIPFSCIDWidth);
    ui->treeWidgetApprovedProposals->setColumnWidth(4, columnSummaryWidth);

    ui->treeWidgetApprovedRecords->setColumnWidth(0, columnDateWidth);
    ui->treeWidgetApprovedRecords->setColumnWidth(1, columnNameWidth);
    ui->treeWidgetApprovedRecords->setColumnWidth(2, columnVoteRatioWidth);
    ui->treeWidgetApprovedRecords->setColumnWidth(3, columnIPFSCIDWidth);
    ui->treeWidgetApprovedRecords->setColumnWidth(4, columnSummaryWidth);


    //ui->tableWidgetApprovedProposals->
    
    //QTreeWidget* treeWidgetApprovedProposals = new QTreeWidget();
    //ui->treeWidgetApprovedProposals->setColumnCount(1);
    //QList<QTreeWidgetItem*> items;
    //for (int i = 0; i < 10; ++i)
    //    items.append(new QTreeWidgetItem((QTreeWidget*)0, QStringList(QString("item: %1").arg(i))));
    
    //ui->treeWidgetApprovedProposals->insertTopLevelItems(0, items);


    //QTreeWidgetItem* row1 = new QTreeWidgetItem(ui->treeWidgetApprovedProposals);
    //row1->setText(0, "Row 1"); //Column 1
    //row1->setText(1, "Data");  //Column 2

    // Row 2
    //QTreeWidgetItem* row2 = new QTreeWidgetItem(ui->treeWidgetApprovedProposals);
    //row2->setText(0, "Row 2"); //Column 1

    //Child row for Row1
    //QTreeWidgetItem* row1_child = new QTreeWidgetItem(row1);
    //row1_child->setText(0, "Row Child");

    //ui->treeWidgetApprovedProposals->setItemWidget(row1_child, 0, row1_child);
    //row1_child->setFirstColumnSpanned(true);

    std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
    for (const auto& pGovObj : objs) {
        if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_RECORD) {
        //if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_RECORD && pGovObj->IsSetRecordLocked()) {
            time_t creationTime = pGovObj->GetCreationTime();
            std::string const plainData = pGovObj->GetDataAsPlainString();
            nlohmann::json jsonData = json::parse(plainData);
            QString voteRatio = QString::number(pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));
	        nlohmann::json summaryData = jsonData["summary"];

            QTableWidgetItem* name = new QTableWidgetItem(QString::fromStdString(jsonData["name"].get<std::string>()));
            QTableWidgetItem* ipfscid = new QTableWidgetItem(QString::fromStdString(jsonData["ipfscid"].get<std::string>()));
            QTableWidgetItem* date = new QTableWidgetItem(QDateTime::fromTime_t(creationTime).toString("MMMM dd, yyyy"));
            QTableWidgetItem* Vote = new QTableWidgetItem(voteRatio);

            ui->tableWidgetApprovedRecords->insertRow(0);
	    std::string summaryName = summaryData["name"].get<std::string>();
	    std::string summaryDesc = summaryData["description"].get<std::string>();
	    QTableWidgetItem* summaryColumn = new QTableWidgetItem(QString::fromStdString(summaryName + ": " + summaryDesc));

	    ui->tableWidgetApprovedRecords->insertRow(0);
            ui->tableWidgetApprovedRecords->setItem(0, 0, date);
            ui->tableWidgetApprovedRecords->setItem(0, 1, name);
            ui->tableWidgetApprovedRecords->setItem(0, 2, Vote);
            ui->tableWidgetApprovedRecords->setItem(0, 3, ipfscid);
	    ui->tableWidgetApprovedRecords->setItem(0, 4, summaryColumn);
        }
    }
    ui->tableWidgetApprovedRecords->sortItems(0, Qt::DescendingOrder);
    ui->tableWidgetApprovedProposals->sortItems(0, Qt::DescendingOrder);
    
    connect(ui->tabWidget, SIGNAL(currentChanged(int)), this, SLOT(tabSelected(int)));
    connect(ui->tableWidgetApprovedRecords, SIGNAL(clicked(QModelIndex)), this,
        SLOT(handleProposalClicked(QModelIndex)));

}

ProposalsPage::~ProposalsPage()
{
    delete ui;
}


void ProposalsPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;

}

void ProposalsPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    /*
    if(model && model->getOptionsModel())
    {
        connect(model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));
        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

    }
*/
}

QStringList ProposalsPage::listProposals() {
    QStringList List;
    std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);

    for (const auto& pGovObj : objs) {
        QString proposalHash = QString::fromStdString(pGovObj->GetDataAsPlainString());
	    List << proposalHash;
	}

    return List;
}

void ProposalsPage::handleProposalClicked(const QModelIndex& index)
{
    if (!clientModel || ShutdownRequested()) {
        return;
    }

    std::string addr = clientModel->getRandomValidMN();
    std::string urltemp;

    if (ui->tabWidget->currentIndex() == 0) {
        QString ipfscid = ui->tableWidgetApprovedRecords->item(index.row(), 3)->text();
        urltemp = "http://" + addr + "/ipfs/" + ipfscid.toUtf8().constData() + "/Index.html";
    } else if (ui->tabWidget->currentIndex() == 1) {
        QString ipfscid = ui->tableWidgetApprovedProposals->item(index.row(), 3)->text();
        urltemp = "http://" + addr + "/ipfs/" + ipfscid.toUtf8().constData() + "/Index.html";
    } else if (ui->tabWidget->currentIndex() == 2) {
        QTreeWidgetItem* item = ui->treeWidgetApprovedRecords->currentItem();
        QString ipfscid = item->text(3);
        urltemp = "http://" + addr + "/ipfs/" + ipfscid.toUtf8().constData() + "/Index.html";
    } else if (ui->tabWidget->currentIndex() == 3) {
        QTreeWidgetItem* item = ui->treeWidgetApprovedProposals->currentItem();
        QString ipfscid = item->text(3);
        //QString ipfscid = ui->treeWidgetApprovedProposals->currentItem()->data(0, Qt::UserRole).toString();
        urltemp = "http://" + addr + "/ipfs/" + ipfscid.toUtf8().constData() + "/Index.html";
    }

    QString url = QString::fromUtf8(urltemp.c_str());
    LogPrintf("ProposalsPage::handleProposalClicked %s\n", urltemp);
    QDesktopServices::openUrl(QUrl(url, QUrl::TolerantMode));

}

void ProposalsPage::tabSelected(int tabIndex)
{
    ui->treeWidgetApprovedProposals->clear();
    ui->treeWidgetApprovedRecords->clear();
    ui->tableWidgetApprovedRecords->clearContents();
    ui->tableWidgetApprovedRecords->setRowCount(0);
    ui->tableWidgetApprovedProposals->clearContents();
    ui->tableWidgetApprovedProposals->setRowCount(0);

    if (tabIndex == 0) {
        std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
        for (const auto& pGovObj : objs) {
            if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_RECORD) {
                //if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_RECORD && pGovObj->IsSetRecordLocked()) {
                time_t creationTime = pGovObj->GetCreationTime();
                std::string const plainData = pGovObj->GetDataAsPlainString();
                nlohmann::json jsonData = json::parse(plainData);
                QString voteRatio = QString::number(pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));

                QTableWidgetItem* name = new QTableWidgetItem(QString::fromStdString(jsonData["name"].get<std::string>()));
                QTableWidgetItem* ipfscid = new QTableWidgetItem(QString::fromStdString(jsonData["ipfscid"].get<std::string>()));
                nlohmann::json summaryData = jsonData["summary"];
                QTableWidgetItem* date = new QTableWidgetItem(QDateTime::fromTime_t(creationTime).toString("MMMM dd, yyyy"));
                QTableWidgetItem* Vote = new QTableWidgetItem(voteRatio);
                std::string summaryName = summaryData["name"].get<std::string>();
                std::string summaryDesc = summaryData["description"].get<std::string>();
                QTableWidgetItem* summaryColumn = new QTableWidgetItem(QString::fromStdString(summaryName + ": " + summaryDesc));

                ui->tableWidgetApprovedRecords->insertRow(0);
                ui->tableWidgetApprovedRecords->setItem(0, 0, date);
                ui->tableWidgetApprovedRecords->setItem(0, 1, name);
                ui->tableWidgetApprovedRecords->setItem(0, 2, Vote);
                ui->tableWidgetApprovedRecords->setItem(0, 3, ipfscid);
                ui->tableWidgetApprovedRecords->setItem(0, 4, summaryColumn);
            }
        }

        connect(ui->tableWidgetApprovedProposals, SIGNAL(clicked(QModelIndex)), this,
            SLOT(handleProposalClicked(QModelIndex)));
    } else if (tabIndex == 1) {
        std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
        for (const auto& pGovObj : objs) {
            if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL) {
                //if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL && pGovObj->IsSetCachedFunding()) {
                time_t creationTime = pGovObj->GetCreationTime();
                std::string const plainData = pGovObj->GetDataAsPlainString();
                nlohmann::json jsonData = json::parse(plainData);
                QString voteRatio = QString::number(pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));

                nlohmann::json summaryData = jsonData["summary"];

                QTableWidgetItem* name = new QTableWidgetItem(QString::fromStdString(jsonData["name"].get<std::string>()));
                QTableWidgetItem* ipfscid = new QTableWidgetItem(QString::fromStdString(jsonData["ipfscid"].get<std::string>()));

                QTableWidgetItem* date = new QTableWidgetItem(QDateTime::fromTime_t(creationTime).toString("MMMM dd, yyyy"));
                QTableWidgetItem* Vote = new QTableWidgetItem(voteRatio);
                std::string summaryName = summaryData["name"].get<std::string>();
                std::string summaryDesc = summaryData["description"].get<std::string>();
                QTableWidgetItem* summaryColumn = new QTableWidgetItem(QString::fromStdString(summaryName + ": " + summaryDesc));

                ui->tableWidgetApprovedProposals->insertRow(0);
                ui->tableWidgetApprovedProposals->setItem(0, 0, date);
                ui->tableWidgetApprovedProposals->setItem(0, 1, name);
                ui->tableWidgetApprovedProposals->setItem(0, 2, Vote);
                ui->tableWidgetApprovedProposals->setItem(0, 3, ipfscid);
                ui->tableWidgetApprovedProposals->setItem(0, 4, summaryColumn);
            }
        }

        connect(ui->tableWidgetApprovedProposals, SIGNAL(clicked(QModelIndex)), this,
            SLOT(handleProposalClicked(QModelIndex)));
    } else if (tabIndex == 3) {
        ui->treeWidgetApprovedProposals->setColumnCount(4);

        std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
        for (const auto& pGovObj : objs) {
            if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL) {
                //if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL && pGovObj->IsSetCachedFunding()) {
                QTreeWidgetItem* row1 = new QTreeWidgetItem(ui->treeWidgetApprovedProposals);
                time_t creationTime = pGovObj->GetCreationTime();
                std::string const plainData = pGovObj->GetDataAsPlainString();
                nlohmann::json jsonData = json::parse(plainData);
                QString voteRatio = QString::number(pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));

                nlohmann::json summaryData = jsonData["summary"];
                std::string summaryName = summaryData["name"].get<std::string>();
                std::string summaryDesc = summaryData["description"].get<std::string>();

                row1->setText(0, (QDateTime::fromTime_t(creationTime).toString("MMMM dd, yyyy"))); //Column 1 - creationTime
                row1->setText(1, QString::fromStdString(summaryName));                             //Column 2 - summaryName
                row1->setText(2, voteRatio);                                                       //Column 3 - voteRatio
                row1->setText(3, QString::fromStdString(jsonData["ipfscid"].get<std::string>()));  //Column 4 - ipfscid

                //Summary child row for Row1
                QTreeWidgetItem* row1_child = new QTreeWidgetItem(row1);
                row1_child->setText(0, QString::fromStdString(summaryDesc));
                row1_child->setFirstColumnSpanned(true);
            }
        }
        connect(ui->treeWidgetApprovedRecords, SIGNAL(clicked(QModelIndex)), this,
            SLOT(handleProposalClicked(QModelIndex)));
        
    } else if (tabIndex == 2) {
        
        ui->treeWidgetApprovedRecords->setColumnCount(4);
        std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
        for (const auto& pGovObj : objs) {
            if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_RECORD) {
                //if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL && pGovObj->IsSetCachedFunding()) {
                QTreeWidgetItem* row1 = new QTreeWidgetItem(ui->treeWidgetApprovedRecords);
                time_t creationTime = pGovObj->GetCreationTime();
                std::string const plainData = pGovObj->GetDataAsPlainString();
                nlohmann::json jsonData = json::parse(plainData);
                QString voteRatio = QString::number(pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));

                nlohmann::json summaryData = jsonData["summary"];
                std::string summaryName = summaryData["name"].get<std::string>();
                std::string summaryDesc = summaryData["description"].get<std::string>();

                row1->setText(0, (QDateTime::fromTime_t(creationTime).toString("MMMM dd, yyyy"))); //Column 1 - creationTime
                row1->setText(1, QString::fromStdString(summaryName));                             //Column 2 - summaryName
                row1->setText(2, voteRatio);                                                       //Column 3 - voteRatio
                row1->setText(3, QString::fromStdString(jsonData["ipfscid"].get<std::string>()));  //Column 4 - ipfscid

                //Summary child row for Row1
                QTreeWidgetItem* row1_child = new QTreeWidgetItem(row1);
                row1_child->setText(0, QString::fromStdString(summaryDesc));
                row1_child->setFirstColumnSpanned(true);
            }
        }
        connect(ui->treeWidgetApprovedRecords, SIGNAL(clicked(QModelIndex)), this,
            SLOT(handleProposalClicked(QModelIndex)));
    }
}

