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
#include "rpcconsole.h"
#include "masternode-sync.h"
#include "governance.h"

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QSettings>
#include <QTimer>
#include <QObject>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringListModel>
#include <QDesktopServices>
#include <QUrl>
#include <ctime>
#include <iomanip>
#include <QTreeWidget>
#include <QStringList>
#include <QProcess>
#include <QDir>
#include <QSysInfo>
#include <QCoreApplication>

#define ICON_OFFSET 16
#define DECORATION_SIZE 54
#define NUM_ITEMS 5
#define NUM_ITEMS_ADV 7

#include "proposalspage.moc"

ProposalsPage::ProposalsPage(const PlatformStyle* platformStyle, QWidget* parent) :
    QWidget(parent), ui(new Ui::ProposalsPage),  clientModel(0)
{
    ui->setupUi(this);

    int columnNameWidth = 225;
    int columnDateWidth = 150;
    int columnIPFSCIDWidth = 300;
    int columnVoteRatioWidth = 200;
    int columnVoteWidth = 100;

    ui->treeWidgetProposals->setColumnWidth(0, columnDateWidth);
    ui->treeWidgetProposals->setColumnWidth(1, columnNameWidth);
    ui->treeWidgetProposals->setColumnWidth(2, columnVoteRatioWidth);
    ui->treeWidgetProposals->setColumnWidth(3, columnIPFSCIDWidth);
    ui->treeWidgetProposals->setColumnWidth(4, columnVoteWidth);
    ui->treeWidgetProposals->header()->setStretchLastSection(false);
    ui->treeWidgetProposals->header()->setSectionResizeMode(0,QHeaderView::Stretch); 
    ui->treeWidgetProposals->header()->setSectionResizeMode(1,QHeaderView::Stretch); 
    ui->treeWidgetProposals->header()->setSectionResizeMode(2,QHeaderView::Stretch);
    ui->treeWidgetProposals->header()->setSectionResizeMode(3,QHeaderView::ResizeToContents); 

    ui->treeWidgetVotingRecords->setColumnWidth(0, columnDateWidth);
    ui->treeWidgetVotingRecords->setColumnWidth(1, columnNameWidth);
    ui->treeWidgetVotingRecords->setColumnWidth(2, columnVoteRatioWidth);
    ui->treeWidgetVotingRecords->setColumnWidth(3, columnIPFSCIDWidth);
    ui->treeWidgetVotingRecords->setColumnWidth(4, columnVoteWidth);
    ui->treeWidgetVotingRecords->header()->setStretchLastSection(false);
    ui->treeWidgetVotingRecords->header()->setSectionResizeMode(0, QHeaderView::Stretch); 
    ui->treeWidgetVotingRecords->header()->setSectionResizeMode(1, QHeaderView::Stretch); 
    ui->treeWidgetVotingRecords->header()->setSectionResizeMode(2, QHeaderView::Stretch); 
    ui->treeWidgetVotingRecords->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents); 

    ui->treeWidgetApprovedRecords->setColumnWidth(0, columnDateWidth);
    ui->treeWidgetApprovedRecords->setColumnWidth(1, columnNameWidth);
    ui->treeWidgetApprovedRecords->setColumnWidth(2, columnVoteRatioWidth);
    ui->treeWidgetApprovedRecords->setColumnWidth(3, columnIPFSCIDWidth);
    ui->treeWidgetApprovedRecords->header()->setStretchLastSection(false);
    ui->treeWidgetApprovedRecords->header()->setSectionResizeMode(0, QHeaderView::Stretch); 
    ui->treeWidgetApprovedRecords->header()->setSectionResizeMode(1, QHeaderView::Stretch); 
    ui->treeWidgetApprovedRecords->header()->setSectionResizeMode(2, QHeaderView::Stretch); 
    ui->treeWidgetApprovedRecords->header()->setSectionResizeMode(3, QHeaderView::Stretch); 
    
    ui->treeWidgetProposals->setColumnCount(5);
    ui->treeWidgetVotingRecords->setColumnCount(5);
    ui->treeWidgetApprovedRecords->setColumnCount(4);
    
    QString rowTip = "Double click to open in your browser.";
    ui->treeWidgetProposals->setToolTip(rowTip);
    ui->treeWidgetVotingRecords->setToolTip(rowTip);
    ui->treeWidgetApprovedRecords->setToolTip(rowTip);

    ui->treeWidgetProposals->setStyleSheet("QTreeView::item { color: #000000; background-color: #ffffff; padding: 5px; height: 18px; line-height: 18px; min-height: 0px; max-height 18px; } QTreeWidget::item:has-children {color: #000000; background-color: #ffffff; padding: 0px; height: 16px; line-height: 16px; min-height: 0px; max-height 16px; }");
    ui->treeWidgetVotingRecords->setStyleSheet("QTreeView::item { color: #000000; background-color: #ffffff;padding: 5px;  height: 18px; line-height: 18x; min-height: 0px; max-height 18px; } QTreeWidget::item:has-children {color: #000000; background-color: #ffffff;  padding: 0px; height: 16px; line-height: 16px; min-height: 0px; max-height 16px; } ");
    ui->treeWidgetApprovedRecords->setStyleSheet("QTreeView::item { color: #000000; background-color: #ffffff; padding: 5px;  height: 18px; line-height: 18px; min-height: 0px;max-height 18px;  } QTreeWidget::item:has-children {color: #000000; background-color: #ffffff; padding: 0px; height: 16px; line-height: 16px; min-height: 0px; max-height 16px; }");

    connect(ui->LaunchHLWAButton, &QPushButton::clicked, this, &ProposalsPage::LaunchHLWAButtonClick);


    QString theme = GUIUtil::getThemeName();
    int govObjCount = 0;

    std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
    for (const auto& pGovObj : objs) {
	if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL && !pGovObj->IsSetCachedDelete()) {
            govObjCount++;
    	    QTreeWidgetItem* row1 = new QTreeWidgetItem(ui->treeWidgetProposals);
 
            time_t creationTime = pGovObj->GetCreationTime();
            QString plainDataJson = QString::fromStdString(pGovObj->GetDataAsPlainString());
            QJsonDocument jsonPlain = QJsonDocument::fromJson(plainDataJson.toUtf8());
            QJsonObject jsonObj = jsonPlain.object();
            QJsonValue jsonSubObj = jsonObj.value(QString("summary"));
            QJsonObject item = jsonSubObj.toObject();
            QJsonValue summaryName = item["name"];
            QJsonValue summaryDesc = item["description"];
            QJsonValue ipfscid = jsonObj["ipfscid"];

            QString voteRatio = QString::number(pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));
            std::string govobjHash = pGovObj->GetHash().ToString();
	    
            QWidget* votingButtons = new QWidget();
            QHBoxLayout* hLayout = new QHBoxLayout();
            votingButtons->setFixedHeight(16);
            hLayout->setSpacing(0);
            hLayout->setContentsMargins(0, 0, 0, 0);

            QPushButton* YesButton = new QPushButton;
            QPushButton* NoButton = new QPushButton;
            QPushButton* AbstainButton = new QPushButton;
            
            YesButton->setIcon(QIcon(":/icons/" + theme + "/vote-yes"));
            QString YesTip = "Send Yes Vote";
            YesButton->setToolTip(YesTip);
            YesButton->setStyleSheet("QPushButton { color: #000000; background-color: #ffffff; }");
            YesButton->setIconSize(QSize(16, 16));

	    NoButton->setIcon(QIcon(":/icons/" + theme + "/vote-no"));
            QString NoTip = "Send No Vote";
            NoButton->setToolTip(NoTip);
            NoButton->setStyleSheet("QPushButton { color: #000000; background-color: #ffffff; }");
            NoButton->setIconSize(QSize(16, 16));

	    AbstainButton->setIcon(QIcon(":/icons/" + theme + "/vote-null"));
            QString AbstainTip = "Send Abstain Vote";
            AbstainButton->setToolTip(AbstainTip);
            AbstainButton->setStyleSheet("QPushButton { color: #000000; background-color: #ffffff;}");
            AbstainButton->setIconSize(QSize(16, 16));
            // Use C++11 lambda expressions to pass parameters in
	        // sendVote method.
	    connect(YesButton, &QPushButton::clicked, this, [=]() { sendVote("yes", govobjHash, YesButton); });
	    connect(NoButton, &QPushButton::clicked, this, [=]() { sendVote("no", govobjHash, NoButton); });
	    connect(AbstainButton, &QPushButton::clicked, this, [=]() { sendVote("abstain", govobjHash, AbstainButton); });

            if (masternodeSync.IsSynced()) {
                YesButton->setDisabled(false);
                NoButton->setDisabled(false);
                AbstainButton->setDisabled(false);
            } else {
                YesButton->setDisabled(true);
                NoButton->setDisabled(true);
                AbstainButton->setDisabled(true);
            }
            vote_outcome_enum_t voteOutcome;
            voteOutcome = findPreviousVote(govobjHash);
         
            switch (voteOutcome) {
            case vote_outcome_enum_t::VOTE_OUTCOME_YES:
                YesButton->setDisabled(true);
                break;
            case vote_outcome_enum_t::VOTE_OUTCOME_NO:
                NoButton->setDisabled(true);
                break;
            case vote_outcome_enum_t::VOTE_OUTCOME_ABSTAIN:
                AbstainButton->setDisabled(true);
                break;
            default:
                break;
	    }

            hLayout->addWidget(YesButton);
            hLayout->addWidget(NoButton);
            hLayout->addWidget(AbstainButton);
            votingButtons->setLayout(hLayout);

            row1->setText(0, (QDateTime::fromTime_t(creationTime).toString("MMMM dd, yyyy"))); //Column 1 - creationTime
            row1->setText(1, summaryName.toString());                                          //Column 2 - summaryName
            row1->setText(2, voteRatio);                                                       //Column 3 - voteRatio
            row1->setText(3, ipfscid.toString());                                              //Column 4 - ipfscid
            ui->treeWidgetProposals->setItemWidget(row1, 4, votingButtons);

            //Summary child row for Row1
            QTreeWidgetItem* row1_child = new QTreeWidgetItem(row1);
            row1_child->setText(0, summaryDesc.toString());
            row1_child->setFirstColumnSpanned(true);
        }
    }
    if (govObjCount == 0) {
        QTreeWidgetItem* row1 = new QTreeWidgetItem(ui->treeWidgetProposals);
        if (masternodeSync.IsSynced()) {
            row1->setText(0, QString("No proposals found."));
        } else {
            row1->setText(0, QString("Please wait until sync is complete."));
        }
        row1->setFirstColumnSpanned(true);
    }

    connect(ui->treeWidgetProposals, SIGNAL(doubleClicked(QModelIndex)), this, 
        SLOT(handleProposalClicked(QModelIndex)));
    connect(ui->treeWidgetVotingRecords, SIGNAL(doubleClicked(QModelIndex)), this,
        SLOT(handleProposalClicked(QModelIndex)));
    connect(ui->treeWidgetApprovedRecords, SIGNAL(doubleClicked(QModelIndex)), this,
        SLOT(handleProposalClicked(QModelIndex)));
    connect(ui->tabWidget, SIGNAL(currentChanged(int)), this, SLOT(tabSelected(int)));

}

ProposalsPage::~ProposalsPage()
{
    delete ui;
}


void ProposalsPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;

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

	//std::string addr = clientModel->getRandomValidMN();
	
	std::string addr = "public.historiasys.network";
    std::string urltemp;

    if (ui->tabWidget->currentIndex() == 0) {
        QTreeWidgetItem* item = ui->treeWidgetProposals->currentItem();
        QString ipfscid;
        if (!item->text(3).isNull()) {
            ipfscid = item->text(3);
        } else {
            ipfscid = item->parent()->text(3);
        }
        urltemp = "http://" + addr + "/ipfs/" + ipfscid.toUtf8().constData() + "/index.html";
    } else if (ui->tabWidget->currentIndex() == 1) {
        QTreeWidgetItem* item = ui->treeWidgetVotingRecords->currentItem();
        QString ipfscid;
        if (!item->text(3).isNull()) {
            ipfscid = item->text(3);
        } else {
            ipfscid = item->parent()->text(3);
        }
        urltemp = "http://" + addr + "/ipfs/" + ipfscid.toUtf8().constData() + "/index.html";
    } else if (ui->tabWidget->currentIndex() == 2) {
        QTreeWidgetItem* item = ui->treeWidgetApprovedRecords->currentItem();
        QString ipfscid;
        if (!item->text(3).isNull()) {
            ipfscid = item->text(3);
        } else {
            ipfscid = item->parent()->text(3);
        }
        urltemp = "http://" + addr + "/ipfs/" + ipfscid.toUtf8().constData() + "/index.html";
    } 
    
    QString url = QString::fromUtf8(urltemp.c_str());
    LogPrintf("ProposalsPage::handleProposalClicked %s\n", urltemp);
    QDesktopServices::openUrl(QUrl(url, QUrl::TolerantMode));

}

void ProposalsPage::tabSelected(int tabIndex)
{
    ui->treeWidgetProposals->clear();
    ui->treeWidgetVotingRecords->clear();
    ui->treeWidgetApprovedRecords->clear();
    QString theme = GUIUtil::getThemeName();
    if (tabIndex == 0) {
        ui->treeWidgetProposals->setColumnCount(5);

        std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);

        int govObjCount = 0;
        for (const auto& pGovObj : objs) {
	    if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL && !pGovObj->IsSetCachedDelete()) {
                govObjCount++;
                QTreeWidgetItem* row1 = new QTreeWidgetItem(ui->treeWidgetProposals);
                time_t creationTime = pGovObj->GetCreationTime();
                QString plainDataJson = QString::fromStdString(pGovObj->GetDataAsPlainString());
                QJsonDocument jsonPlain = QJsonDocument::fromJson(plainDataJson.toUtf8());
                QJsonObject jsonObj = jsonPlain.object();
                QJsonValue jsonSubObj = jsonObj.value(QString("summary"));
                QJsonObject item = jsonSubObj.toObject();
                QJsonValue summaryName = item["name"];
                QJsonValue summaryDesc = item["description"];
                QJsonValue ipfscid = jsonObj["ipfscid"];

                QString voteRatio = QString::number(pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));
		        std::string govobjHash = pGovObj->GetHash().ToString();
		
                QWidget* votingButtons = new QWidget();
                QHBoxLayout* hLayout = new QHBoxLayout();
                votingButtons->setFixedHeight(16);
                hLayout->setSpacing(0);
                hLayout->setContentsMargins(0, 0, 0, 0);

                QPushButton* YesButton = new QPushButton;
                QPushButton* NoButton = new QPushButton;
                QPushButton* AbstainButton = new QPushButton;
            
                YesButton->setIcon(QIcon(":/icons/" + theme + "/vote-yes"));
                QString YesTip = "Send Yes Vote";
                YesButton->setToolTip(YesTip);
                YesButton->setStyleSheet("QPushButton { color: #000000; background-color: #ffffff; }");
                YesButton->setIconSize(QSize(16, 16));

                NoButton->setIcon(QIcon(":/icons/" + theme + "/vote-no"));
                QString NoTip = "Send No Vote";
                NoButton->setToolTip(NoTip);
                NoButton->setStyleSheet("QPushButton { color: #000000; background-color: #ffffff; }");
                NoButton->setIconSize(QSize(16, 16));

                AbstainButton->setIcon(QIcon(":/icons/" + theme + "/vote-null"));
                QString AbstainTip = "Send Abstain Vote";
                AbstainButton->setToolTip(AbstainTip);
                AbstainButton->setStyleSheet("QPushButton { color: #000000; background-color: #ffffff;}");
                AbstainButton->setIconSize(QSize(16, 16));

                connect(YesButton, &QPushButton::clicked, this, [=]() { sendVote("yes", govobjHash, YesButton); });
                connect(NoButton, &QPushButton::clicked, this, [=]() { sendVote("no", govobjHash, NoButton); });
                connect(AbstainButton, &QPushButton::clicked, this, [=](){ sendVote("abstain", govobjHash, AbstainButton); });
                
                if (masternodeSync.IsSynced()) {
                    YesButton->setDisabled(false);
                    NoButton->setDisabled(false);
                    AbstainButton->setDisabled(false);
                } else {
                    YesButton->setDisabled(true);
                    NoButton->setDisabled(true);
                    AbstainButton->setDisabled(true);
                }
                        
		vote_outcome_enum_t voteOutcome = findPreviousVote(govobjHash);
		switch(voteOutcome) {
		    case vote_outcome_enum_t::VOTE_OUTCOME_YES: YesButton->setDisabled(true); break;
		    case vote_outcome_enum_t::VOTE_OUTCOME_NO: NoButton->setDisabled(true); break;
		    case vote_outcome_enum_t::VOTE_OUTCOME_ABSTAIN: AbstainButton->setDisabled(true); break;
		    default: break;
		}
		    
                hLayout->addWidget(YesButton);
                hLayout->addWidget(NoButton);
                hLayout->addWidget(AbstainButton);
                votingButtons->setLayout(hLayout);

                row1->setText(0, (QDateTime::fromTime_t(creationTime).toString("MMMM dd, yyyy"))); //Column 1 - creationTime
                row1->setText(1, summaryName.toString());                                          //Column 2 - summaryName
                row1->setText(2, voteRatio);                                                       //Column 3 - voteRatio
                row1->setText(3, ipfscid.toString());                                              //Column 4 - ipfscid
                ui->treeWidgetProposals->setItemWidget(row1, 4, votingButtons);

                //Summary child row for Row1
                QTreeWidgetItem* row1_child = new QTreeWidgetItem(row1);
                row1_child->setText(0, summaryDesc.toString());
                row1_child->setFirstColumnSpanned(true);
            }
        }
        if (govObjCount == 0) {
            QTreeWidgetItem* row1 = new QTreeWidgetItem(ui->treeWidgetProposals);
            if (masternodeSync.IsSynced()) {
                row1->setText(0, QString("No proposals found."));
            } else {
                row1->setText(0, QString("Please wait until sync is complete."));
            }
            row1->setFirstColumnSpanned(true);
        }
        disconnect(ui->treeWidgetProposals, SIGNAL(doubleClicked(QModelIndex)), this,
            SLOT(handleProposalClicked(QModelIndex)));
        connect(ui->treeWidgetProposals, SIGNAL(doubleClicked(QModelIndex)), this,
            SLOT(handleProposalClicked(QModelIndex)));
         
    } else if (tabIndex == 1) {
        ui->treeWidgetVotingRecords->setColumnCount(5);
        int govObjCount = 0;
        std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);

        for (const auto& pGovObj : objs) {
	    if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_RECORD && !pGovObj->IsSetRecordPastSuperBlock() && !pGovObj->IsSetCachedDelete()) {
                govObjCount++;
                QTreeWidgetItem* row1 = new QTreeWidgetItem(ui->treeWidgetVotingRecords);
                time_t creationTime = pGovObj->GetCreationTime();
                QString plainDataJson = QString::fromStdString(pGovObj->GetDataAsPlainString());
                QJsonDocument jsonPlain = QJsonDocument::fromJson(plainDataJson.toUtf8());
                QJsonObject jsonObj = jsonPlain.object();
                QJsonValue jsonSubObj = jsonObj.value(QString("summary"));
                QJsonObject item = jsonSubObj.toObject();
                QJsonValue summaryName = item["name"];
                QJsonValue summaryDesc = item["description"];
                QJsonValue ipfscid = jsonObj["ipfscid"];

                QString voteRatio = QString::number(pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));
		std::string govobjHash = pGovObj->GetHash().ToString();
		
                QWidget* votingButtons = new QWidget();
                QHBoxLayout* hLayout = new QHBoxLayout();
                votingButtons->setFixedHeight(16);
                hLayout->setSpacing(0);
                hLayout->setContentsMargins(0, 0, 0, 0);

                QPushButton* YesButton = new QPushButton;
                QPushButton* NoButton = new QPushButton;
                QPushButton* AbstainButton = new QPushButton;
            
                YesButton->setIcon(QIcon(":/icons/" + theme + "/vote-yes"));
                QString YesTip = "Send Yes Vote";
                YesButton->setToolTip(YesTip);
                YesButton->setStyleSheet("QPushButton { color: #000000; background-color: #ffffff; }");
                YesButton->setIconSize(QSize(16, 16));

                NoButton->setIcon(QIcon(":/icons/" + theme + "/vote-no"));
                QString NoTip = "Send No Vote";
                NoButton->setToolTip(NoTip);
                NoButton->setStyleSheet("QPushButton { color: #000000; background-color: #ffffff; }");
                NoButton->setIconSize(QSize(16, 16));

                AbstainButton->setIcon(QIcon(":/icons/" + theme + "/vote-null"));
                QString AbstainTip = "Send Abstain Vote";
                AbstainButton->setToolTip(AbstainTip);
                AbstainButton->setStyleSheet("QPushButton { color: #000000; background-color: #ffffff;}");
                AbstainButton->setIconSize(QSize(16, 16));

                connect(YesButton, &QPushButton::clicked, this, [=]() { sendVote("yes", govobjHash, YesButton); });
	        connect(NoButton, &QPushButton::clicked, this, [=]() { sendVote("no", govobjHash, NoButton); });
	        connect(AbstainButton, &QPushButton::clicked, this, [=]() { sendVote("abstain", govobjHash, AbstainButton);	});
                        
                if (masternodeSync.IsSynced()) {
                    YesButton->setDisabled(false);
                    NoButton->setDisabled(false);
                    AbstainButton->setDisabled(false);
                } else {
                    YesButton->setDisabled(true);
                    NoButton->setDisabled(true);
                    AbstainButton->setDisabled(true);
                }
                        
		vote_outcome_enum_t voteOutcome = findPreviousVote(govobjHash);
		switch(voteOutcome) {
		    case vote_outcome_enum_t::VOTE_OUTCOME_YES: YesButton->setDisabled(true); break;
		    case vote_outcome_enum_t::VOTE_OUTCOME_NO: NoButton->setDisabled(true); break;
		    case vote_outcome_enum_t::VOTE_OUTCOME_ABSTAIN: AbstainButton->setDisabled(true); break;
		    default: break;
		}
		    
                hLayout->addWidget(YesButton);
                hLayout->addWidget(NoButton);
                hLayout->addWidget(AbstainButton);
                votingButtons->setLayout(hLayout);

                row1->setText(0, (QDateTime::fromTime_t(creationTime).toString("MMMM dd, yyyy"))); //Column 1 - creationTime
                row1->setText(1, summaryName.toString());                                          //Column 2 - summaryName
                row1->setText(2, voteRatio);                                                       //Column 3 - voteRatio
                row1->setText(3, ipfscid.toString());                                              //Column 4 - ipfscid
                ui->treeWidgetVotingRecords->setItemWidget(row1, 4, votingButtons);

                //Summary child row for Row1
                QTreeWidgetItem* row1_child = new QTreeWidgetItem(row1);
                row1_child->setText(0, summaryDesc.toString());
                row1_child->setFirstColumnSpanned(true);
            }
        }
        if (govObjCount == 0) {
            QTreeWidgetItem* row1 = new QTreeWidgetItem(ui->treeWidgetVotingRecords);
            if (masternodeSync.IsSynced()) {
                row1->setText(0, QString("No records found."));
            } else {
                row1->setText(0, QString("Please wait until sync is complete."));
            }
            row1->setFirstColumnSpanned(true);
        }
        disconnect(ui->treeWidgetVotingRecords, SIGNAL(doubelClicked(QModelIndex)), this,
            SLOT(handleProposalClicked(QModelIndex)));
        connect(ui->treeWidgetVotingRecords, SIGNAL(doubelClicked(QModelIndex)), this,
            SLOT(handleProposalClicked(QModelIndex)));
        
    } else if (tabIndex == 2) {
        
        ui->treeWidgetApprovedRecords->setColumnCount(4);
        int govObjCount = 0;
        std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);

        for (const auto& pGovObj : objs) {
            if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_RECORD && pGovObj->IsSetPermLocked() && !pGovObj->IsSetCachedFunding() && pGovObj->IsSetRecordPastSuperBlock() ) {
                govObjCount++;
                QTreeWidgetItem* row1 = new QTreeWidgetItem(ui->treeWidgetApprovedRecords);
                time_t creationTime = pGovObj->GetCreationTime();

                QString plainDataJson = QString::fromStdString(pGovObj->GetDataAsPlainString());
                QJsonDocument jsonPlain = QJsonDocument::fromJson(plainDataJson.toUtf8());
                QJsonObject jsonObj = jsonPlain.object();
                QJsonValue jsonSubObj = jsonObj.value(QString("summary"));
                QJsonObject item = jsonSubObj.toObject();
                QJsonValue summaryName = item["name"];
                QJsonValue summaryDesc = item["description"];
                QJsonValue ipfscid = jsonObj["ipfscid"];
  
                QString voteRatio = QString::number(pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));
		std::string govobjHash = pGovObj->GetHash().ToString();
		
                row1->setText(0, (QDateTime::fromTime_t(creationTime).toString("MMMM dd, yyyy"))); //Column 1 - creationTime
                row1->setText(1, summaryName.toString());                                          //Column 2 - summaryName
                row1->setText(2, voteRatio);                                                       //Column 3 - voteRatio
                row1->setText(3, ipfscid.toString());                                              //Column 4 - ipfscid

                //Summary child row for Row1
                QTreeWidgetItem* row1_child = new QTreeWidgetItem(row1);
                row1_child->setText(0, summaryDesc.toString());
                row1_child->setFirstColumnSpanned(true);
            }
        }
        if (govObjCount == 0) {
            QTreeWidgetItem* row1 = new QTreeWidgetItem(ui->treeWidgetApprovedRecords);
            if (masternodeSync.IsSynced()) {
                row1->setText(0, QString("No records found."));
            } else {
                row1->setText(0, QString("Please wait until sync is complete."));
            }
            row1->setFirstColumnSpanned(true);
        }
        disconnect(ui->treeWidgetApprovedRecords, SIGNAL(doubleClicked(QModelIndex)), this,
            SLOT(handleProposalClicked(QModelIndex)));
        connect(ui->treeWidgetApprovedRecords, SIGNAL(doubleClicked(QModelIndex)), this,
            SLOT(handleProposalClicked(QModelIndex)));
     } 
}

void ProposalsPage::handleVoteButtonClicked(VoteButton outcome, const std::string &govobjHash, QPushButton *button)
{
    switch (outcome) {
        case VoteButton::VOTE_YES: sendVote("yes", govobjHash, button); break;
        case VoteButton::VOTE_NO: sendVote("no", govobjHash, button); break;
        case VoteButton::VOTE_ABSTAIN: sendVote("abstain", govobjHash, button);
	    break;
    }
}


void ProposalsPage::LaunchHLWAButtonClick()
{
    QString appDirPath = QCoreApplication::applicationDirPath();
    QString appImagePath;

#if defined(Q_OS_LINUX)
    appImagePath = appDirPath + "/hlwa/hlwa.appimage";
#elif defined(Q_OS_MAC)
    appImagePath = appDirPath + "/hlwa/Historia Local Web App-1.6.0.dmg"; // Or use .app if it's an application bundle
#elif defined(Q_OS_WIN)
    appImagePath = appDirPath + "\\hlwa\\hlwa.exe";
#endif

    appImagePath = QDir::toNativeSeparators(appImagePath);

    if (!QFile::exists(appImagePath)) {
        QMessageBox::critical(this, "Error", "The application file does not exist at: " + appImagePath);
        return;
    }

    // Windows-specific: running the application in a hidden console
    QProcess* process = new QProcess(this);
#if defined(Q_OS_WIN)
    QString command = "cmd.exe";
    QStringList arguments;
    arguments << "/C"
              << "start"
              << "\"\""
              << "/B" << appImagePath;

    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW; // This flag ensures no console window is shown
    });
    process->start(command, arguments);
#else
    // Non-Windows: just start normally
    QProcess::startDetached(appImagePath);
#endif

    // Optional: Check if the process starts successfully
    if (!process->waitForStarted()) {
        QMessageBox::critical(this, "Error", "Failed to start the application at: " + appImagePath);
    }
}



void ProposalsPage::sendVote(std::string outcome, const std::string &govobjHash,
			     QPushButton *button)
{
    QMessageBox *msgBox = new QMessageBox(this);

    // Check first of all if wallet is in masternode mode, otherwise it
    // can't vote anyway.
    if (! fMasternodeMode) {
	msgBox->setIcon(QMessageBox::Critical);
	msgBox->setText(QString("You must setup a masternode or voting node "
				"before you are able to vote."));
	msgBox->exec();
	return;
    }
    // Confirm box
    QString voteSelection = QString::fromStdString(outcome);
    voteSelection[0] = voteSelection[0].toUpper();
    QString confirm = QString("You are about to vote " + voteSelection +
			      " on this proposal or record. \nAre you sure you"
			      " want to do this?");
    msgBox->setText(confirm);
    msgBox->setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
    msgBox->setIcon(QMessageBox::Information);
    int sure = msgBox->exec();
    if (sure != QMessageBox::Ok) return;
    
    std::string command = ("gobject vote-many " + govobjHash + " funding " + outcome);
    std::string result;
    try {
	    RPCConsole::RPCExecuteCommandLine(result, command);
	    msgBox->setIcon(QMessageBox::Information);
	    button->setDisabled(true);
    } catch (UniValue &e) {
	    result =  find_value(e, "message").get_str();
	    LogPrintf("ProposalsPage::sendVote %s\n", result);
	    msgBox->setIcon(QMessageBox::Warning);
	    msgBox->setText(QString::fromStdString(result));
	    msgBox->exec();
	    return;
    } catch (std::exception &goverror) {
	    msgBox->setIcon(QMessageBox::Critical);
	    const char *error = goverror.what();
	    msgBox->setText(QString::fromStdString(error));
	    msgBox->exec();
	    LogPrintf("CGovernanceException %s\n", error);
	    return;
    }

    QJsonDocument info = QJsonDocument::fromJson(QString::fromStdString(result).toUtf8());
    QString showInfo = info.object()["overall"].toString();
    msgBox->setStandardButtons(QMessageBox::Ok);
    msgBox->setText(showInfo);
    msgBox->exec();
}

vote_outcome_enum_t ProposalsPage::findPreviousVote(const std::string &govobjHash)
{
    std::string result, collateralIndex, collateralHash;
    QJsonDocument qJsonDoc;
    QJsonObject jsonResult;
    std::string mnoutputs = "masternode outputs";
    if (!fMasternodeMode) {
        return vote_outcome_enum_t::VOTE_OUTCOME_NONE;
    }
    try {
	RPCConsole::RPCExecuteCommandLine(result, mnoutputs);
	qJsonDoc = QJsonDocument::fromJson(QString::fromStdString(result).toUtf8());
	if (!qJsonDoc.isNull()) {
	    QJsonObject jsonResult = qJsonDoc.object();
	    QString qCollateralHash = jsonResult.keys()[0];
	    collateralIndex = jsonResult[qCollateralHash].toString().toStdString();
	    collateralHash = qCollateralHash.toStdString();
	}
    } catch (UniValue &e) {
	return vote_outcome_enum_t::VOTE_OUTCOME_NONE;
    }

    COutPoint mnCollateralOutpoint;
    uint256 txid, hash;
    txid.SetHex(collateralHash);
    hash.SetHex(govobjHash);
    mnCollateralOutpoint = COutPoint(txid, (uint32_t)atoi(collateralIndex));

    LOCK(governance.cs);

    CGovernanceObject* pGovObj = governance.FindGovernanceObject(hash);
    if (pGovObj == nullptr) {
	    return vote_outcome_enum_t::VOTE_OUTCOME_NONE;
    }
    std::map<int64_t, vote_outcome_enum_t> nodeVotes;
    std::vector<CGovernanceVote> vecVotes = governance.GetCurrentVotes(hash, mnCollateralOutpoint);
    for (const auto& vote : vecVotes) {
	vote_outcome_enum_t outcome = vote.GetOutcome();
	int64_t timestamp = vote.GetTimestamp();
	nodeVotes.insert({timestamp, outcome});
    }

    vote_outcome_enum_t recentVoteOutcome = nodeVotes.begin()->second;
    return recentVoteOutcome;
}
