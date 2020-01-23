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
#include <QPushButton>
#include <QStringList>

#define ICON_OFFSET 16
#define DECORATION_SIZE 54
#define NUM_ITEMS 5
#define NUM_ITEMS_ADV 7

#include "proposalspage.moc"

ProposalsPage::ProposalsPage(const PlatformStyle* platformStyle, QWidget* parent) :
    QWidget(parent),
    ui(new Ui::ProposalsPage),
    clientModel(0)
{
    ui->setupUi(this);

    int columnNameWidth = 225;
    int columnDateWidth = 150;
    int columnIPFSCIDWidth = 233;
    int columnVoteRatioWidth = 200;
    int columnVoteWidth = 25;

    ui->treeWidgetProposals->setColumnWidth(0, columnDateWidth);
    ui->treeWidgetProposals->setColumnWidth(1, columnNameWidth);
    ui->treeWidgetProposals->setColumnWidth(2, columnVoteRatioWidth);
    ui->treeWidgetProposals->setColumnWidth(3, columnIPFSCIDWidth);
    ui->treeWidgetProposals->setColumnWidth(4, columnVoteWidth);

    ui->treeWidgetVotingRecords->setColumnWidth(0, columnDateWidth);
    ui->treeWidgetVotingRecords->setColumnWidth(1, columnNameWidth);
    ui->treeWidgetVotingRecords->setColumnWidth(2, columnVoteRatioWidth);
    ui->treeWidgetVotingRecords->setColumnWidth(3, columnIPFSCIDWidth);
    ui->treeWidgetVotingRecords->setColumnWidth(4, columnVoteWidth);

    ui->treeWidgetApprovedRecords->setColumnWidth(0, columnDateWidth);
    ui->treeWidgetApprovedRecords->setColumnWidth(1, columnNameWidth);
    ui->treeWidgetApprovedRecords->setColumnWidth(2, columnVoteRatioWidth);
    ui->treeWidgetApprovedRecords->setColumnWidth(3, columnIPFSCIDWidth);

    ui->treeWidgetProposals->setColumnCount(5);
    ui->treeWidgetVotingRecords->setColumnCount(5);
    ui->treeWidgetApprovedRecords->setColumnCount(4);
    
    QString rowTip = "Double click to open in your browser.";
    ui->treeWidgetProposals->setToolTip(rowTip);
    ui->treeWidgetVotingRecords->setToolTip(rowTip);
    ui->treeWidgetApprovedRecords->setToolTip(rowTip);
    
    ui->treeWidgetProposals->setStyleSheet("QTreeView::item { padding: 1px }");
    ui->treeWidgetVotingRecords->setStyleSheet("QTreeView::item { padding: 1px }");
    ui->treeWidgetApprovedRecords->setStyleSheet("QTreeView::item { padding: 9px }");
    
    QString theme = GUIUtil::getThemeName();
    
    std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
    for (const auto& pGovObj : objs) {
        if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL) {
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

            QPushButton* YesButton = new QPushButton;
            QPushButton* NoButton = new QPushButton;
            QPushButton* AbstainButton = new QPushButton;
            
            YesButton->setIcon(QIcon(":/icons/" + theme + "/vote-yes"));
            YesButton->setIconSize(QSize(16, 16));
            YesButton->setFixedSize(QSize(16, 16));
            QString YesTip = "Send Yes Vote";
            YesButton->setToolTip(YesTip);
            
            NoButton->setIcon(QIcon(":/icons/" + theme + "/vote-no"));
            NoButton->setIconSize(QSize(16, 16));
            NoButton->setFixedSize(QSize(16, 16));
            QString NoTip = "Send No Vote";
            NoButton->setToolTip(NoTip);

            AbstainButton->setIcon(QIcon(":/icons/" + theme + "/vote-null"));
            AbstainButton->setIconSize(QSize(16, 16));
            AbstainButton->setFixedSize(QSize(16, 16));
            QString AbstainTip = "Send Abstain Vote";
            AbstainButton->setToolTip(AbstainTip);

            votingButtons->setStyleSheet("QPushButton { background-color: #FFFFFF; border: 1px solid white; border-radius: 7px; padding: 1px; text-align: center; }");
	    // Use C++11 lambda expressions to pass parameters in
	    // sendVote method.
	    connect(YesButton, &QPushButton::clicked, this,
		    [=]() { sendVote("yes", govobjHash); });
	    connect(NoButton, &QPushButton::clicked, this,
		    [=]() { sendVote("no", govobjHash); });
	    connect(AbstainButton, &QPushButton::clicked, this,
		    [=]() { sendVote("abstain", govobjHash); });

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
    connect(ui->treeWidgetProposals, SIGNAL(doubleClicked(QModelIndex)), this,
        SLOT(handleProposalClicked(QModelIndex)));

    connect(ui->tabWidget, SIGNAL(currentChanged(int)), this, SLOT(tabSelected(int)));
    connect(ui->treeWidgetProposals, SIGNAL(doubleClicked(QModelIndex)), this,
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
        QTreeWidgetItem* item = ui->treeWidgetProposals->currentItem();
        QString ipfscid;
        if (!item->text(3).isNull()) {
            ipfscid = item->text(3);
        } else {
            ipfscid = item->parent()->text(3);
        }
        urltemp = "http://" + addr + "/ipfs/" + ipfscid.toUtf8().constData() + "/Index.html";
    } else if (ui->tabWidget->currentIndex() == 1) {
        QTreeWidgetItem* item = ui->treeWidgetVotingRecords->currentItem();
        QString ipfscid;
        if (!item->text(3).isNull()) {
            ipfscid = item->text(3);
        } else {
            ipfscid = item->parent()->text(3);
        }
        urltemp = "http://" + addr + "/ipfs/" + ipfscid.toUtf8().constData() + "/Index.html";
    } else if (ui->tabWidget->currentIndex() == 2) {
        QTreeWidgetItem* item = ui->treeWidgetApprovedRecords->currentItem();
        QString ipfscid;
        if (!item->text(3).isNull()) {
            ipfscid = item->text(3);
        } else {
            ipfscid = item->parent()->text(3);
        }
        urltemp = "http://" + addr + "/ipfs/" + ipfscid.toUtf8().constData() + "/Index.html";
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
        for (const auto& pGovObj : objs) {
            if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL) {
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

                QPushButton* YesButton = new QPushButton;
                QPushButton* NoButton = new QPushButton;
                QPushButton* AbstainButton = new QPushButton;

                YesButton->setIcon(QIcon(":/icons/" + theme + "/vote-yes"));
                YesButton->setIconSize(QSize(16, 16));
                YesButton->setFixedSize(QSize(16, 16));
                QString YesTip = "Send Yes Vote";
                YesButton->setToolTip(YesTip);

                NoButton->setIcon(QIcon(":/icons/" + theme + "/vote-no"));
                NoButton->setIconSize(QSize(16, 16));
                NoButton->setFixedSize(QSize(16, 16));
                QString NoTip = "Send No Vote";
                NoButton->setToolTip(NoTip);

                AbstainButton->setIcon(QIcon(":/icons/" + theme + "/vote-null"));
                AbstainButton->setIconSize(QSize(16, 16));
                AbstainButton->setFixedSize(QSize(16, 16));
                QString AbstainTip = "Send Abstain Vote";
                AbstainButton->setToolTip(AbstainTip);

                votingButtons->setStyleSheet("QPushButton { background-color: #FFFFFF; border: 1px solid white; border-radius: 7px; padding: 1px; text-align: center; }");
		connect(YesButton, &QPushButton::clicked, this,
			[=]() { sendVote("yes", govobjHash); });
		connect(NoButton, &QPushButton::clicked, this,
			[=]() { sendVote("no", govobjHash); });
		connect(AbstainButton, &QPushButton::clicked, this,
			[=]() { sendVote("abstain", govobjHash); });
		    
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
        connect(ui->treeWidgetProposals, SIGNAL(doubleClicked(QModelIndex)), this,
            SLOT(handleProposalClicked(QModelIndex)));
        
    } else if (tabIndex == 1) {
        ui->treeWidgetVotingRecords->setColumnCount(5);

        std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
        for (const auto& pGovObj : objs) {
            if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_RECORD && !pGovObj->IsSetRecordPastSuperBlock()) {

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

                QPushButton* YesButton = new QPushButton;
                QPushButton* NoButton = new QPushButton;
                QPushButton* AbstainButton = new QPushButton;

                YesButton->setIcon(QIcon(":/icons/" + theme + "/vote-yes"));
                YesButton->setIconSize(QSize(16, 16));
                YesButton->setFixedSize(QSize(16, 16));
                QString YesTip = "Send Yes Vote";
                YesButton->setToolTip(YesTip);

                NoButton->setIcon(QIcon(":/icons/" + theme + "/vote-no"));
                NoButton->setIconSize(QSize(16, 16));
                NoButton->setFixedSize(QSize(16, 16));
                QString NoTip = "Send No Vote";
                NoButton->setToolTip(NoTip);

                AbstainButton->setIcon(QIcon(":/icons/" + theme + "/vote-null"));
                AbstainButton->setIconSize(QSize(16, 16));
                AbstainButton->setFixedSize(QSize(16, 16));
                QString AbstainTip = "Send Abstain Vote";
                AbstainButton->setToolTip(AbstainTip);

                votingButtons->setStyleSheet("QPushButton { background-color: #FFFFFF; border: 1px solid white; border-radius: 7px; padding: 1px; text-align: center; }");
		connect(YesButton, &QPushButton::clicked, this,
			[=]() { sendVote("yes", govobjHash); });
		connect(NoButton, &QPushButton::clicked, this,
			[=]() { sendVote("no", govobjHash); });
		connect(AbstainButton, &QPushButton::clicked, this,
			[=]() { sendVote("abstain", govobjHash); });
		    
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
        connect(ui->treeWidgetVotingRecords, SIGNAL(doubelClicked(QModelIndex)), this,
            SLOT(handleProposalClicked(QModelIndex)));
        
    } else if (tabIndex == 2) {
        
        ui->treeWidgetApprovedRecords->setColumnCount(4);

        std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
        for (const auto& pGovObj : objs) {
            if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_RECORD && pGovObj->IsSetPermLocked() && !pGovObj->IsSetCachedFunding() && pGovObj->IsSetRecordPastSuperBlock() ) {

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
		
                QWidget* votingButtons = new QWidget();
                QHBoxLayout* hLayout = new QHBoxLayout();

                QPushButton* YesButton = new QPushButton;
                QPushButton* NoButton = new QPushButton;
                QPushButton* AbstainButton = new QPushButton;

                YesButton->setIcon(QIcon(":/icons/" + theme + "/add"));
                YesButton->setIconSize(QSize(16, 16));
                YesButton->setFixedSize(QSize(16, 16));
                QString YesTip = "Send Yes Vote";
                YesButton->setToolTip(YesTip);

                NoButton->setIcon(QIcon(":/icons/" + theme + "/address-book"));
                NoButton->setIconSize(QSize(16, 16));
                NoButton->setFixedSize(QSize(16, 16));
                QString NoTip = "Send No Vote";
                NoButton->setToolTip(NoTip);

                AbstainButton->setIcon(QIcon(":/icons/" + theme + "/browse"));
                AbstainButton->setIconSize(QSize(16, 16));
                AbstainButton->setFixedSize(QSize(16, 16));
                QString AbstainTip = "Send Abstain Vote";
                AbstainButton->setToolTip(AbstainTip);

                votingButtons->setStyleSheet("QPushButton { background-color: #FFFFFF; border: 1px solid white; border-radius: 7px; padding: 1px; text-align: center; }");
		connect(YesButton, &QPushButton::clicked, this,
			[=]() { sendVote("yes", govobjHash); });
		connect(NoButton, &QPushButton::clicked, this,
			[=]() { sendVote("no", govobjHash); });
		connect(AbstainButton, &QPushButton::clicked, this,
			[=]() { sendVote("abstain", govobjHash); });

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
        connect(ui->treeWidgetApprovedRecords, SIGNAL(doubleClicked(QModelIndex)), this,
            SLOT(handleProposalClicked(QModelIndex)));
     } 
}

void ProposalsPage::handleVoteButtonClicked(VoteButton outcome,
					    const std::string &govobjHash)
{
    switch (outcome) {
    case VoteButton::VOTE_YES: sendVote("yes", govobjHash); break;
    case VoteButton::VOTE_NO: sendVote("no", govobjHash); break;
    case VoteButton::VOTE_ABSTAIN: sendVote("abstain", govobjHash); break;
    }
}

void ProposalsPage::sendVote(std::string outcome, const std::string &govobjHash)
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
    
    std::string command = ("gobject vote-many " + govobjHash + " funding "
			   + outcome);
    std::string result;
    try {
	RPCConsole::RPCExecuteCommandLine(result, command);
	msgBox->setIcon(QMessageBox::Information);
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

    QJsonDocument info =
	QJsonDocument::fromJson(QString::fromStdString(result).toUtf8());
    QString showInfo = info.object()["overall"].toString();
    msgBox->setStandardButtons(QMessageBox::Ok);
    msgBox->setText(showInfo);
    msgBox->exec();
}
