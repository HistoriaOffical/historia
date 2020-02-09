// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2018 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/historia-config.h"
#endif

#include "rpcconsole.h"
#include "ui_debugwindow.h"

#include "bantablemodel.h"
#include "clientmodel.h"
#include "guiutil.h"
#include "platformstyle.h"

#include "chainparams.h"
#include "netbase.h"
#include "rpc/server.h"
#include "rpc/client.h"
#include "util.h"
#include "masternode-sync.h"
#include "ipfs-utils.h"
#include <openssl/crypto.h>
#include <boost/utility/binary.hpp>
#include <univalue.h>

#ifdef ENABLE_WALLET
#include <db_cxx.h>
#endif

#include <QDir>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QSettings>
#include <QSignalMapper>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <QStringList>
#include <QDesktopServices> 

#if QT_VERSION < 0x050000
#include <QUrl>
#endif

// TODO: add a scrollback limit, as there is currently none
// TODO: make it possible to filter out categories (esp debug messages when implemented)
// TODO: receive errors and debug messages through ClientModel

const int CONSOLE_HISTORY = 50;
const QSize FONT_RANGE(4, 40);
const char fontSizeSettingsKey[] = "consoleFontSize";

const TrafficGraphData::GraphRange INITIAL_TRAFFIC_GRAPH_SETTING = TrafficGraphData::Range_30m;

// Repair parameters
const QString SALVAGEWALLET("-salvagewallet");
const QString RESCAN("-rescan");
const QString ZAPTXES1("-zapwallettxes=1");
const QString ZAPTXES2("-zapwallettxes=2");
const QString UPGRADEWALLET("-upgradewallet");
const QString REINDEX("-reindex");

const struct {
    const char *url;
    const char *source;
} ICON_MAPPING[] = {
    {"cmd-request", "tx_input"},
    {"cmd-reply", "tx_output"},
    {"cmd-error", "tx_output"},
    {"misc", "tx_inout"},
    {NULL, NULL}
};

struct _votingNodeInfo votingNodeInfo;

namespace {

// don't add private key handling cmd's to the history
const QStringList historyFilter = QStringList()
    << "importprivkey"
    << "importmulti"
    << "signmessagewithprivkey"
    << "signrawtransaction"
    << "walletpassphrase"
    << "walletpassphrasechange"
    << "encryptwallet";

}

/* Object for executing console RPC commands in a separate thread.
*/
class RPCExecutor : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void request(const QString &command);

Q_SIGNALS:
    void reply(int category, const QString &command);
};

/** Class for handling RPC timers
 * (used for e.g. re-locking the wallet after a timeout)
 */
class QtRPCTimerBase: public QObject, public RPCTimerBase
{
    Q_OBJECT
public:
    QtRPCTimerBase(boost::function<void(void)>& _func, int64_t millis):
        func(_func)
    {
        timer.setSingleShot(true);
        connect(&timer, SIGNAL(timeout()), this, SLOT(timeout()));
        timer.start(millis);
    }
    ~QtRPCTimerBase() {}
private Q_SLOTS:
    void timeout() { func(); }
private:
    QTimer timer;
    boost::function<void(void)> func;
};

class QtRPCTimerInterface: public RPCTimerInterface
{
public:
    ~QtRPCTimerInterface() {}
    const char *Name() override { return "Qt"; }
    RPCTimerBase* NewTimer(boost::function<void(void)>& func, int64_t millis) override
    {
        return new QtRPCTimerBase(func, millis);
    }
};


#include "rpcconsole.moc"

/**
 * Split shell command line into a list of arguments and optionally execute the command(s).
 * Aims to emulate \c bash and friends.
 *
 * - Command nesting is possible with parenthesis; for example: validateaddress(getnewaddress())
 * - Arguments are delimited with whitespace or comma
 * - Extra whitespace at the beginning and end and between arguments will be ignored
 * - Text can be "double" or 'single' quoted
 * - The backslash \c \ is used as escape character
 *   - Outside quotes, any character can be escaped
 *   - Within double quotes, only escape \c " and backslashes before a \c " or another backslash
 *   - Within single quotes, no escaping is possible and no special interpretation takes place
 *
 * @param[out]   result      stringified Result from the executed command(chain)
 * @param[in]    strCommand  Command line to split
 * @param[in]    fExecute    set true if you want the command to be executed
 * @param[out]   pstrFilteredOut  Command line, filtered to remove any sensitive data
 */

bool RPCConsole::RPCParseCommandLine(std::string &strResult, const std::string &strCommand, const bool fExecute, std::string * const pstrFilteredOut)
{
    std::vector< std::vector<std::string> > stack;
    stack.push_back(std::vector<std::string>());

    enum CmdParseState
    {
        STATE_EATING_SPACES,
        STATE_EATING_SPACES_IN_ARG,
        STATE_EATING_SPACES_IN_BRACKETS,
        STATE_ARGUMENT,
        STATE_SINGLEQUOTED,
        STATE_DOUBLEQUOTED,
        STATE_ESCAPE_OUTER,
        STATE_ESCAPE_DOUBLEQUOTED,
        STATE_COMMAND_EXECUTED,
        STATE_COMMAND_EXECUTED_INNER
    } state = STATE_EATING_SPACES;
    std::string curarg;
    UniValue lastResult;
    unsigned nDepthInsideSensitive = 0;
    size_t filter_begin_pos = 0, chpos;
    std::vector<std::pair<size_t, size_t>> filter_ranges;

    auto add_to_current_stack = [&](const std::string& strArg) {
        if (stack.back().empty() && (!nDepthInsideSensitive) && historyFilter.contains(QString::fromStdString(strArg), Qt::CaseInsensitive)) {
            nDepthInsideSensitive = 1;
            filter_begin_pos = chpos;
        }
        // Make sure stack is not empty before adding something
        if (stack.empty()) {
            stack.push_back(std::vector<std::string>());
        }
        stack.back().push_back(strArg);
    };

    auto close_out_params = [&]() {
        if (nDepthInsideSensitive) {
            if (!--nDepthInsideSensitive) {
                assert(filter_begin_pos);
                filter_ranges.push_back(std::make_pair(filter_begin_pos, chpos));
                filter_begin_pos = 0;
            }
        }
        stack.pop_back();
    };

    std::string strCommandTerminated = strCommand;
    if (strCommandTerminated.back() != '\n')
        strCommandTerminated += "\n";
    for (chpos = 0; chpos < strCommandTerminated.size(); ++chpos)
    {
        char ch = strCommandTerminated[chpos];
        switch(state)
        {
            case STATE_COMMAND_EXECUTED_INNER:
            case STATE_COMMAND_EXECUTED:
            {
                bool breakParsing = true;
                switch(ch)
                {
                    case '[': curarg.clear(); state = STATE_COMMAND_EXECUTED_INNER; break;
                    default:
                        if (state == STATE_COMMAND_EXECUTED_INNER)
                        {
                            if (ch != ']')
                            {
                                // append char to the current argument (which is also used for the query command)
                                curarg += ch;
                                break;
                            }
                            if (curarg.size() && fExecute)
                            {
                                // if we have a value query, query arrays with index and objects with a string key
                                UniValue subelement;
                                if (lastResult.isArray())
                                {
                                    for(char argch: curarg)
                                        if (!std::isdigit(argch))
                                            throw std::runtime_error("Invalid result query");
                                    subelement = lastResult[atoi(curarg.c_str())];
                                }
                                else if (lastResult.isObject())
                                    subelement = find_value(lastResult, curarg);
                                else
                                    throw std::runtime_error("Invalid result query"); //no array or object: abort
                                lastResult = subelement;
                            }

                            state = STATE_COMMAND_EXECUTED;
                            break;
                        }
                        // don't break parsing when the char is required for the next argument
                        breakParsing = false;

                        // pop the stack and return the result to the current command arguments
                        close_out_params();

                        // don't stringify the json in case of a string to avoid doublequotes
                        if (lastResult.isStr())
                            curarg = lastResult.get_str();
                        else
                            curarg = lastResult.write(2);

                        // if we have a non empty result, use it as stack argument otherwise as general result
                        if (curarg.size())
                        {
                            if (stack.size())
                                add_to_current_stack(curarg);
                            else
                                strResult = curarg;
                        }
                        curarg.clear();
                        // assume eating space state
                        state = STATE_EATING_SPACES;
                }
                if (breakParsing)
                    break;
            }
            case STATE_ARGUMENT: // In or after argument
            case STATE_EATING_SPACES_IN_ARG:
            case STATE_EATING_SPACES_IN_BRACKETS:
            case STATE_EATING_SPACES: // Handle runs of whitespace
                switch(ch)
            {
                case '"': state = STATE_DOUBLEQUOTED; break;
                case '\'': state = STATE_SINGLEQUOTED; break;
                case '\\': state = STATE_ESCAPE_OUTER; break;
                case '(': case ')': case '\n':
                    if (state == STATE_EATING_SPACES_IN_ARG)
                        throw std::runtime_error("Invalid Syntax");
                    if (state == STATE_ARGUMENT)
                    {
                        if (ch == '(' && stack.size() && stack.back().size() > 0)
                        {
                            if (nDepthInsideSensitive) {
                                ++nDepthInsideSensitive;
                            }
                            stack.push_back(std::vector<std::string>());
                        }

                        // don't allow commands after executed commands on baselevel
                        if (!stack.size())
                            throw std::runtime_error("Invalid Syntax");

                        add_to_current_stack(curarg);
                        curarg.clear();
                        state = STATE_EATING_SPACES_IN_BRACKETS;
                    }
                    if ((ch == ')' || ch == '\n') && stack.size() > 0)
                    {
                        if (fExecute) {
                            // Convert argument list to JSON objects in method-dependent way,
                            // and pass it along with the method name to the dispatcher.
                            JSONRPCRequest req;
                            req.params = RPCConvertValues(stack.back()[0], std::vector<std::string>(stack.back().begin() + 1, stack.back().end()));
                            req.strMethod = stack.back()[0];
                            lastResult = tableRPC.execute(req);
                        }

                        state = STATE_COMMAND_EXECUTED;
                        curarg.clear();
                    }
                    break;
                case ' ': case ',': case '\t':
                    if(state == STATE_EATING_SPACES_IN_ARG && curarg.empty() && ch == ',')
                        throw std::runtime_error("Invalid Syntax");

                    else if(state == STATE_ARGUMENT) // Space ends argument
                    {
                        add_to_current_stack(curarg);
                        curarg.clear();
                    }
                    if ((state == STATE_EATING_SPACES_IN_BRACKETS || state == STATE_ARGUMENT) && ch == ',')
                    {
                        state = STATE_EATING_SPACES_IN_ARG;
                        break;
                    }
                    state = STATE_EATING_SPACES;
                    break;
                default: curarg += ch; state = STATE_ARGUMENT;
            }
                break;
            case STATE_SINGLEQUOTED: // Single-quoted string
                switch(ch)
            {
                case '\'': state = STATE_ARGUMENT; break;
                default: curarg += ch;
            }
                break;
            case STATE_DOUBLEQUOTED: // Double-quoted string
                switch(ch)
            {
                case '"': state = STATE_ARGUMENT; break;
                case '\\': state = STATE_ESCAPE_DOUBLEQUOTED; break;
                default: curarg += ch;
            }
                break;
            case STATE_ESCAPE_OUTER: // '\' outside quotes
                curarg += ch; state = STATE_ARGUMENT;
                break;
            case STATE_ESCAPE_DOUBLEQUOTED: // '\' in double-quoted text
                if(ch != '"' && ch != '\\') curarg += '\\'; // keep '\' for everything but the quote and '\' itself
                curarg += ch; state = STATE_DOUBLEQUOTED;
                break;
        }
    }
    if (pstrFilteredOut) {
        if (STATE_COMMAND_EXECUTED == state) {
            assert(!stack.empty());
            close_out_params();
        }
        *pstrFilteredOut = strCommand;
        for (auto i = filter_ranges.rbegin(); i != filter_ranges.rend(); ++i) {
            pstrFilteredOut->replace(i->first, i->second - i->first, "(…)");
        }
    }
    switch(state) // final state
    {
        case STATE_COMMAND_EXECUTED:
            if (lastResult.isStr())
                strResult = lastResult.get_str();
            else
                strResult = lastResult.write(2);
        case STATE_ARGUMENT:
        case STATE_EATING_SPACES:
            return true;
        default: // ERROR to end in one of the other states
            return false;
    }
}

void RPCExecutor::request(const QString &command)
{
    try
    {
        std::string result;
        std::string executableCommand = command.toStdString() + "\n";
        if(!RPCConsole::RPCExecuteCommandLine(result, executableCommand))
        {
            Q_EMIT reply(RPCConsole::CMD_ERROR, QString("Parse error: unbalanced ' or \""));
            return;
        }
        Q_EMIT reply(RPCConsole::CMD_REPLY, QString::fromStdString(result));
    }
    catch (UniValue& objError)
    {
        try // Nice formatting for standard-format error
        {
            int code = find_value(objError, "code").get_int();
            std::string message = find_value(objError, "message").get_str();
            Q_EMIT reply(RPCConsole::CMD_ERROR, QString::fromStdString(message) + " (code " + QString::number(code) + ")");
        }
        catch (const std::runtime_error&) // raised when converting to invalid type, i.e. missing code or message
        {   // Show raw JSON object
            Q_EMIT reply(RPCConsole::CMD_ERROR, QString::fromStdString(objError.write()));
        }
    }
    catch (const std::exception& e)
    {
        Q_EMIT reply(RPCConsole::CMD_ERROR, QString("Error: ") + QString::fromStdString(e.what()));
    }
}

RPCConsole::RPCConsole(const PlatformStyle *_platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::RPCConsole),
    clientModel(0),
    historyPtr(0),
    platformStyle(_platformStyle),
    peersTableContextMenu(0),
    banTableContextMenu(0),
    consoleFontSize(0)
{
    ui->setupUi(this);
    GUIUtil::restoreWindowGeometry("nRPCConsoleWindow", this->size(), this);

    ui->openDebugLogfileButton->setToolTip(ui->openDebugLogfileButton->toolTip().arg(tr(PACKAGE_NAME)));

    QString theme = GUIUtil::getThemeName();
    if (platformStyle->getImagesOnButtons()) {
        ui->openDebugLogfileButton->setIcon(QIcon(":/icons/" + theme + "/export"));
    }
    // Needed on Mac also
    ui->clearButton->setIcon(QIcon(":/icons/" + theme + "/remove"));
    ui->fontBiggerButton->setIcon(QIcon(":/icons/" + theme + "/fontbigger"));
    ui->fontSmallerButton->setIcon(QIcon(":/icons/" + theme + "/fontsmaller"));

    // Install event filter for up and down arrow
    ui->lineEdit->installEventFilter(this);
    ui->messagesWidget->installEventFilter(this);

    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));
    connect(ui->fontBiggerButton, SIGNAL(clicked()), this, SLOT(fontBigger()));
    connect(ui->fontSmallerButton, SIGNAL(clicked()), this, SLOT(fontSmaller()));
    connect(ui->btnClearTrafficGraph, SIGNAL(clicked()), ui->trafficGraph, SLOT(clear()));
    
    // Wallet Repair Buttons
    // connect(ui->btn_salvagewallet, SIGNAL(clicked()), this, SLOT(walletSalvage()));
    // Disable salvage option in GUI, it's way too powerful and can lead to funds loss
    ui->btn_salvagewallet->setEnabled(false);
    connect(ui->btn_rescan, SIGNAL(clicked()), this, SLOT(walletRescan()));
    connect(ui->btn_zapwallettxes1, SIGNAL(clicked()), this, SLOT(walletZaptxes1()));
    connect(ui->btn_zapwallettxes2, SIGNAL(clicked()), this, SLOT(walletZaptxes2()));
    connect(ui->btn_upgradewallet, SIGNAL(clicked()), this, SLOT(walletUpgrade()));
    connect(ui->btn_reindex, SIGNAL(clicked()), this, SLOT(walletReindex()));
    connect(ui->btn_genvoterkeys, SIGNAL(clicked()), this, SLOT(genVoterKeys()));
    connect(ui->btn_sendvotingnodetx, SIGNAL(clicked()), this, SLOT(sendVotingNodeTx()));
    connect(ui->btn_sendprotx, SIGNAL(clicked()), this, SLOT(sendProTx()));
    connect(ui->btn_revokevotingnode, SIGNAL(clicked()), this, SLOT(revokeProTx()));
    connect(ui->btn_readinstruct, SIGNAL(clicked()), this, SLOT(openDocUrl()));
    // set library version labels
#ifdef ENABLE_WALLET
    ui->berkeleyDBVersion->setText(DbEnv::version(0, 0, 0));
    std::string walletPath = GetDataDir().string();
    walletPath += QDir::separator().toLatin1() + GetArg("-wallet", "wallet.dat");
    ui->wallet_path->setText(QString::fromStdString(walletPath));
#else
    ui->label_berkeleyDBVersion->hide();
    ui->berkeleyDBVersion->hide();
#endif
    // Register RPC timer interface
    rpcTimerInterface = new QtRPCTimerInterface();
    // avoid accidentally overwriting an existing, non QTThread
    // based timer interface
    RPCSetTimerInterfaceIfUnset(rpcTimerInterface);

    setTrafficGraphRange(INITIAL_TRAFFIC_GRAPH_SETTING);
    
    ui->peerHeading->setText(tr("Select a peer to view detailed information."));
    
    QSettings settings;
    consoleFontSize = settings.value(fontSizeSettingsKey, QFontInfo(QFont()).pointSize()).toInt();
    clear();
}

RPCConsole::~RPCConsole()
{
    GUIUtil::saveWindowGeometry("nRPCConsoleWindow", this);
    RPCUnsetTimerInterface(rpcTimerInterface);
    delete rpcTimerInterface;
    delete ui;
}

bool RPCConsole::eventFilter(QObject* obj, QEvent *event)
{
    if(event->type() == QEvent::KeyPress) // Special key handling
    {
        QKeyEvent *keyevt = static_cast<QKeyEvent*>(event);
        int key = keyevt->key();
        Qt::KeyboardModifiers mod = keyevt->modifiers();
        switch(key)
        {
        case Qt::Key_Up: if(obj == ui->lineEdit) { browseHistory(-1); return true; } break;
        case Qt::Key_Down: if(obj == ui->lineEdit) { browseHistory(1); return true; } break;
        case Qt::Key_PageUp: /* pass paging keys to messages widget */
        case Qt::Key_PageDown:
            if(obj == ui->lineEdit)
            {
                QApplication::postEvent(ui->messagesWidget, new QKeyEvent(*keyevt));
                return true;
            }
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            // forward these events to lineEdit
            if(obj == autoCompleter->popup()) {
                autoCompleter->popup()->hide();
                QApplication::postEvent(ui->lineEdit, new QKeyEvent(*keyevt));
                return true;
            }
            break;
        default:
            // Typing in messages widget brings focus to line edit, and redirects key there
            // Exclude most combinations and keys that emit no text, except paste shortcuts
            if(obj == ui->messagesWidget && (
                  (!mod && !keyevt->text().isEmpty() && key != Qt::Key_Tab) ||
                  ((mod & Qt::ControlModifier) && key == Qt::Key_V) ||
                  ((mod & Qt::ShiftModifier) && key == Qt::Key_Insert)))
            {
                ui->lineEdit->setFocus();
                QApplication::postEvent(ui->lineEdit, new QKeyEvent(*keyevt));
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void RPCConsole::populateAdditionalInfo(const int i, QString blsPrivateKey)
{
    if (i == 0)
    {
        QString qAdditionalInfo = QString::fromStdString("It will take approximate 1 hour to setup a voting node.Do not close or restart your wallet until instructed to otherwise you will have to start over.After you have read the instructions, please click the \"Generate Voting Node Keys\" to begin this process.");
        if (qAdditionalInfo.size() > 96)
        {
            int breakline = qAdditionalInfo.indexOf('.');
            while (breakline != -1)
            {
                qAdditionalInfo.insert(breakline + 1, '\n');
                breakline = qAdditionalInfo.indexOf('.', breakline + 1);
            }
        }
        ui->AdditionalInfo->setText(qAdditionalInfo);
    }
    else if (i == 1)
    {
        QString qAdditionalInfo = QString::fromStdString("Next, please click on the \"Send Voting Node Transaction\" button.This will send a 100 HTA collateral transaction to your own wallet.You will lose a minor amount in transaction fees.");
        if (qAdditionalInfo.size() > 96)
        {
            int breakline = qAdditionalInfo.indexOf('.');
            while (breakline != -1)
            {
                qAdditionalInfo.insert(breakline + 1, '\n');
                breakline = qAdditionalInfo.indexOf('.', breakline + 1);
                
            }
            
        }
        ui->AdditionalInfo->setText(qAdditionalInfo);
        
    }
    else if (i == 2)
    {
        QString qAdditionalInfo = QString::fromStdString("Finally, enter a unique value in the Identity field above.Then click on the \"Register Voting Node\" button once it becomes enabled.This will send a special voter registration transaction.You will lose a minor amount in transaction fees.");
        if (qAdditionalInfo.size() > 96)
        {
            int breakline = qAdditionalInfo.indexOf('.');
            while (breakline != -1)
            {
                qAdditionalInfo.insert(breakline + 1, '\n');
                breakline = qAdditionalInfo.indexOf('.', breakline + 1);
            }
        }
        ui->AdditionalInfo->setText(qAdditionalInfo);
    }
    else if (i == 3)
    {
        QString qAdditionalInfo = QString::fromStdString("Your Voting Node is will be registered on the network after one block. You must add the following lines to your wallet configuration file and restart the wallet to finalize registration:\n\nmasternode=1\nmasternodecollateral=100\nmasternodeblsprivkey=<blsSecretKeyFromAbove>");
        qAdditionalInfo += blsPrivateKey;
        if (qAdditionalInfo.size() > 96)
        {
            int breakline = qAdditionalInfo.indexOf('.');
            while (breakline != -1)
            {
                qAdditionalInfo.insert(breakline + 1, '\n');
                breakline = qAdditionalInfo.indexOf('.', breakline + 1);
            }
        }
        ui->AdditionalInfo->setText(qAdditionalInfo);
    } else if (i == 4) {
        QString qAdditionalInfo = QString::fromStdString("The above values will be filled after your node has been verfied by the network.\nIf you need to move your coins you must hit the Revoke Node button, remove the masternode settings from your wallet configuation file, and restart your wallet.Your voting node will no longer be able to vote on records and proposals");
        qAdditionalInfo += blsPrivateKey;
        if (qAdditionalInfo.size() > 96) {
            int breakline = qAdditionalInfo.indexOf('.');
            while (breakline != -1) {
                qAdditionalInfo.insert(breakline + 1, '\n');
                breakline = qAdditionalInfo.indexOf('.', breakline + 1);
            }
        }
        ui->AdditionalInfo->setText(qAdditionalInfo);
    } else if (i == 5) {
        QString qAdditionalInfo = QString::fromStdString("Your voting node has now been revoked.Please remove the masternode settings from your wallet configuation file and restart your wallet.");
        qAdditionalInfo += blsPrivateKey;
        if (qAdditionalInfo.size() > 96) {
            int breakline = qAdditionalInfo.indexOf('.');
            while (breakline != -1) {
                qAdditionalInfo.insert(breakline + 1, '\n');
                breakline = qAdditionalInfo.indexOf('.', breakline + 1);
            }
        }
        ui->AdditionalInfo->setText(qAdditionalInfo);
    } else if (i == -1) {
        QString qAdditionalInfo = QString::fromStdString("Please wait until the blockchain is in synced. You should read the instructions while you wait.");
        qAdditionalInfo += blsPrivateKey;
        if (qAdditionalInfo.size() > 96) {
            int breakline = qAdditionalInfo.indexOf('.');
            while (breakline != -1) {
                qAdditionalInfo.insert(breakline + 1, '\n');
                breakline = qAdditionalInfo.indexOf('.', breakline + 1);
            }
        }
        ui->AdditionalInfo->setText(qAdditionalInfo);
    } else if (i == 5000) {
        QString qAdditionalInfo = QString::fromStdString("This node is already setup as a masternode. Setup voting node disabled ");
        qAdditionalInfo += blsPrivateKey;
        if (qAdditionalInfo.size() > 96) {
            int breakline = qAdditionalInfo.indexOf('.');
            while (breakline != -1) {
                qAdditionalInfo.insert(breakline + 1, '\n');
                breakline = qAdditionalInfo.indexOf('.', breakline + 1);
            }
        }
        ui->AdditionalInfo->setText(qAdditionalInfo);
    }
    
}

void RPCConsole::setClientModel(ClientModel *model)
{
    clientModel = model;
    ui->trafficGraph->setClientModel(model);
    if (model && clientModel->getPeerTableModel() && clientModel->getBanTableModel()) {
        // Keep up to date with client
        setNumConnections(model->getNumConnections());
        connect(model, SIGNAL(numConnectionsChanged(int)), this, SLOT(setNumConnections(int)));

        setNumBlocks(model->getNumBlocks(), model->getLastBlockDate(), model->getVerificationProgress(NULL), false);
        connect(model, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(setNumBlocks(int,QDateTime,double,bool)));

        updateNetworkState();
        connect(model, SIGNAL(networkActiveChanged(bool)), this, SLOT(setNetworkActive(bool)));

        connect(model, SIGNAL(masternodeListChanged()), this, SLOT(updateMasternodeCount()));
        clientModel->refreshMasternodeList();

        updateTrafficStats(model->getTotalBytesRecv(), model->getTotalBytesSent());
        connect(model, SIGNAL(bytesChanged(quint64,quint64)), this, SLOT(updateTrafficStats(quint64, quint64)));

        connect(model, SIGNAL(mempoolSizeChanged(long,size_t)), this, SLOT(setMempoolSize(long,size_t)));
        connect(model, SIGNAL(islockCountChanged(size_t)), this, SLOT(setInstantSendLockCount(size_t)));

        // set up peer table
        ui->peerWidget->setModel(model->getPeerTableModel());
        ui->peerWidget->verticalHeader()->hide();
        ui->peerWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui->peerWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->peerWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
        ui->peerWidget->setContextMenuPolicy(Qt::CustomContextMenu);
        ui->peerWidget->setColumnWidth(PeerTableModel::Address, ADDRESS_COLUMN_WIDTH);
        ui->peerWidget->setColumnWidth(PeerTableModel::Subversion, SUBVERSION_COLUMN_WIDTH);
        ui->peerWidget->setColumnWidth(PeerTableModel::Ping, PING_COLUMN_WIDTH);
        ui->peerWidget->horizontalHeader()->setStretchLastSection(true);

        // create peer table context menu actions
        QAction* disconnectAction = new QAction(tr("&Disconnect"), this);
        QAction* banAction1h      = new QAction(tr("Ban for") + " " + tr("1 &hour"), this);
        QAction* banAction24h     = new QAction(tr("Ban for") + " " + tr("1 &day"), this);
        QAction* banAction7d      = new QAction(tr("Ban for") + " " + tr("1 &week"), this);
        QAction* banAction365d    = new QAction(tr("Ban for") + " " + tr("1 &year"), this);

        // create peer table context menu
        peersTableContextMenu = new QMenu(this);
        peersTableContextMenu->addAction(disconnectAction);
        peersTableContextMenu->addAction(banAction1h);
        peersTableContextMenu->addAction(banAction24h);
        peersTableContextMenu->addAction(banAction7d);
        peersTableContextMenu->addAction(banAction365d);

        // Add a signal mapping to allow dynamic context menu arguments.
        // We need to use int (instead of int64_t), because signal mapper only supports
        // int or objects, which is okay because max bantime (1 year) is < int_max.
        QSignalMapper* signalMapper = new QSignalMapper(this);
        signalMapper->setMapping(banAction1h, 60*60);
        signalMapper->setMapping(banAction24h, 60*60*24);
        signalMapper->setMapping(banAction7d, 60*60*24*7);
        signalMapper->setMapping(banAction365d, 60*60*24*365);
        connect(banAction1h, SIGNAL(triggered()), signalMapper, SLOT(map()));
        connect(banAction24h, SIGNAL(triggered()), signalMapper, SLOT(map()));
        connect(banAction7d, SIGNAL(triggered()), signalMapper, SLOT(map()));
        connect(banAction365d, SIGNAL(triggered()), signalMapper, SLOT(map()));
        connect(signalMapper, SIGNAL(mapped(int)), this, SLOT(banSelectedNode(int)));

        // peer table context menu signals
        connect(ui->peerWidget, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showPeersTableContextMenu(const QPoint&)));
        connect(disconnectAction, SIGNAL(triggered()), this, SLOT(disconnectSelectedNode()));

        // peer table signal handling - update peer details when selecting new node
        connect(ui->peerWidget->selectionModel(), SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)),
            this, SLOT(peerSelected(const QItemSelection &, const QItemSelection &)));
        // peer table signal handling - update peer details when new nodes are added to the model
        connect(model->getPeerTableModel(), SIGNAL(layoutChanged()), this, SLOT(peerLayoutChanged()));
        // peer table signal handling - cache selected node ids
        connect(model->getPeerTableModel(), SIGNAL(layoutAboutToBeChanged()), this, SLOT(peerLayoutAboutToChange()));
        
        // set up ban table
        ui->banlistWidget->setModel(model->getBanTableModel());
        ui->banlistWidget->verticalHeader()->hide();
        ui->banlistWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui->banlistWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->banlistWidget->setSelectionMode(QAbstractItemView::SingleSelection);
        ui->banlistWidget->setContextMenuPolicy(Qt::CustomContextMenu);
        ui->banlistWidget->setColumnWidth(BanTableModel::Address, BANSUBNET_COLUMN_WIDTH);
        ui->banlistWidget->setColumnWidth(BanTableModel::Bantime, BANTIME_COLUMN_WIDTH);
        ui->banlistWidget->horizontalHeader()->setStretchLastSection(true);

        // create ban table context menu action
        QAction* unbanAction = new QAction(tr("&Unban"), this);

        // create ban table context menu
        banTableContextMenu = new QMenu(this);
        banTableContextMenu->addAction(unbanAction);

        // ban table context menu signals
        connect(ui->banlistWidget, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showBanTableContextMenu(const QPoint&)));
        connect(unbanAction, SIGNAL(triggered()), this, SLOT(unbanSelectedNode()));

        // ban table signal handling - clear peer details when clicking a peer in the ban table
        connect(ui->banlistWidget, SIGNAL(clicked(const QModelIndex&)), this, SLOT(clearSelectedNode()));
        // ban table signal handling - ensure ban table is shown or hidden (if empty)
        connect(model->getBanTableModel(), SIGNAL(layoutChanged()), this, SLOT(showOrHideBanTableIfRequired()));
        showOrHideBanTableIfRequired();

        ui->btn_sendvotingnodetx->setDisabled(true);
        ui->btn_sendprotx->setDisabled(true);
        if (masternodeSync.IsSynced()) {
            ui->btn_genvoterkeys->setDisabled(true);
            populateAdditionalInfo(0, "");
        } else {
            ui->btn_genvoterkeys->setDisabled(false);
            populateAdditionalInfo(-1, "");
        }
        ui->btn_revokevotingnode->hide();
        ui->btn_revokevotingnode->setDisabled(true);


        if (fMasternodeMode) {
            ui->btn_sendvotingnodetx->hide();
            ui->btn_sendprotx->hide();
            ui->btn_genvoterkeys->hide();
            ui->btn_revokevotingnode->show();
        } 
        // Provide initial values
        ui->clientVersion->setText(model->formatFullVersion());
        ui->clientUserAgent->setText(model->formatSubVersion());
        ui->dataDir->setText(model->dataDir());
        ui->startupTime->setText(model->formatClientStartupTime());
        ui->networkName->setText(QString::fromStdString(Params().NetworkIDString()));

        //Setup autocomplete and attach it
        QStringList wordList;
        std::vector<std::string> commandList = tableRPC.listCommands();
        for (size_t i = 0; i < commandList.size(); ++i)
        {
            wordList << commandList[i].c_str();
        }

        autoCompleter = new QCompleter(wordList, this);
        ui->lineEdit->setCompleter(autoCompleter);
        autoCompleter->popup()->installEventFilter(this);
        // Start thread to execute RPC commands.
        startExecutor();
    }
    if (!model) {
        // Client model is being set to 0, this means shutdown() is about to be called.
        // Make sure we clean up the executor thread
        Q_EMIT stopExecutor();
        thread.wait();
    }
}

void RPCConsole::setTransactionTableModel(TransactionTableModel *model) {
    this->transactionTableModel = model;

}

static QString categoryClass(int category)
{
    switch(category)
    {
    case RPCConsole::CMD_REQUEST:  return "cmd-request"; break;
    case RPCConsole::CMD_REPLY:    return "cmd-reply"; break;
    case RPCConsole::CMD_ERROR:    return "cmd-error"; break;
    default:                       return "misc";
    }
}

void RPCConsole::fontBigger()
{
    setFontSize(consoleFontSize+1);
}

void RPCConsole::fontSmaller()
{
    setFontSize(consoleFontSize-1);
}

void RPCConsole::setFontSize(int newSize)
{
    QSettings settings;

    //don't allow a insane font size
    if (newSize < FONT_RANGE.width() || newSize > FONT_RANGE.height())
        return;

    // temp. store the console content
    QString str = ui->messagesWidget->toHtml();

    // replace font tags size in current content
    str.replace(QString("font-size:%1pt").arg(consoleFontSize), QString("font-size:%1pt").arg(newSize));

    // store the new font size
    consoleFontSize = newSize;
    settings.setValue(fontSizeSettingsKey, consoleFontSize);

    // clear console (reset icon sizes, default stylesheet) and re-add the content
    float oldPosFactor = 1.0 / ui->messagesWidget->verticalScrollBar()->maximum() * ui->messagesWidget->verticalScrollBar()->value();
    clear(false);
    ui->messagesWidget->setHtml(str);
    ui->messagesWidget->verticalScrollBar()->setValue(oldPosFactor * ui->messagesWidget->verticalScrollBar()->maximum());
}

void sendToFeeSource();


void RPCConsole::preSetupVotingTab()
{
    connect(clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)),
	    this, SLOT(collateralReady()));
    bool showhide;
    
    if (!fMasternodeMode) {
	    votingNodeInfo.ownerKeyAddr.empty() ? showhide = 1 : showhide = 0;
	    ui->btn_genvoterkeys->setDisabled(!showhide);
	    ui->btn_revokevotingnode->hide();
	    connect(ui->nodeId, &QLineEdit::textEdited, this, &RPCConsole::nodeIdReady);
	    setupVotingTab();

    } else {
        std::string strMasterNodeCollateral = GetArg("-masternodecollateral", "");
        if (std::stoi(strMasterNodeCollateral) != 5000) {
            ui->btn_genvoterkeys->setDisabled(true);
            ui->btn_revokevotingnode->setDisabled(true);
            ui->btn_genvoterkeys->hide();
            ui->btn_sendprotx->hide();
            ui->btn_sendvotingnodetx->hide();
            ui->btn_revokevotingnode->show();
            ui->btn_revokevotingnode->setDisabled(true);
            fetchVotingNodeInfo();
        } else {
            populateAdditionalInfo(5000, QString::fromStdString(""));
            ui->btn_genvoterkeys->setDisabled(true);
            ui->btn_revokevotingnode->setDisabled(true);
            ui->btn_genvoterkeys->hide();
            ui->btn_sendprotx->hide();
            ui->btn_sendvotingnodetx->hide();
            ui->btn_revokevotingnode->hide();
            ui->btn_revokevotingnode->setDisabled(true);
        }
    }
}

void RPCConsole::fetchCollateralAddress()
{
    std::string strResult;
    QString collateralHash, collateralAddress;
    int confirmations = 0;

    try {
	    collateralHash = votingNodeInfo.collateralHash;
	    std::string getTX = "gettransaction " + collateralHash.toStdString();
	    RPCConsole::RPCExecuteCommandLine(strResult, getTX);
	    QJsonDocument qJsonDoc = QJsonDocument::fromJson(QString::fromStdString(strResult).toUtf8());
	    QJsonObject jsonResult = qJsonDoc.object();
	    confirmations = jsonResult.find(QString("confirmations")).value().toInt();
	    QJsonValue txDetails = jsonResult.value("details");
	    QJsonObject details = txDetails.toArray()[0].toObject();
	    collateralAddress = details.value("address").toString();
	    votingNodeInfo.collateralConfirmations = confirmations;
	    ui->collateralAddress->setText(collateralAddress);
	    ui->collateralHash->setText(collateralHash);
    } catch (UniValue &e) {
	    return;
    }
}

QString fetchProTx(std::string &proTxHash)
{
    std::string strResult;
    RPCConsole::RPCExecuteCommandLine(strResult, "gettransaction " + proTxHash);
    QJsonDocument qJsonDoc =
	QJsonDocument::fromJson(QString::fromStdString(strResult).toUtf8());
    QJsonObject jsonResult = qJsonDoc.object();
    QJsonValue txDetails = jsonResult.value("details");
    QJsonObject details = txDetails.toArray()[0].toObject();
    QString feeSourceAddr = details.value("address").toString();

    return feeSourceAddr;
}

void RPCConsole::fetchMasternodeInfo()
{
    std::string strResult, strProTxHash, nodeStatus;
    uint256 proTxHash;
    QString feeKey;
    std::string strPrivKey;
    
    try {
	    RPCConsole::RPCExecuteCommandLine(strResult, "masternode status");
	    QJsonDocument qJsonDoc = QJsonDocument::fromJson(QString::fromStdString(strResult).toUtf8());
	    QJsonObject jsonResult = qJsonDoc.object();
	    strProTxHash = jsonResult.value("proTxHash").toString().toStdString();
	    votingNodeInfo.proTxHash = strProTxHash;
	    proTxHash.SetHex(strProTxHash);
	    votingNodeInfo.collateralHash = jsonResult.value("collateralHash").toString();
        votingNodeInfo.collateralIndex = jsonResult[votingNodeInfo.collateralHash].toString().toStdString();
	    nodeStatus = jsonResult.value("status").toString().toStdString();

	    QJsonObject dmnState = jsonResult.value("dmnState").toObject();
	    QString ownerKeyAddr = dmnState.value("ownerAddress").toString();
	    ui->ownerKey->setText(ownerKeyAddr);
	
	    QString feeKey = fetchProTx(strProTxHash);
	    votingNodeInfo.feeSourceAddr = feeKey;
	    ui->feeKey->setText(feeKey);

	    strPrivKey = GetArg("-masternodeblsprivkey", "");
	    ui->blsSecret->setText(QString::fromStdString(strPrivKey));
 
    } catch (UniValue &e) {
	     return;
    }

    CDeterministicMNCPtr mn;
    if (!strProTxHash.empty()) {
	    auto mnList = clientModel->getMasternodeList();
	    mn = mnList.GetMN(proTxHash);
	    ui->btn_revokevotingnode->setDisabled(false);
    }
    if (mn != NULL) {
	    std::string pubKey = mn->pdmnState->pubKeyOperator.Get().ToString();
	    if (pubKey == "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000") {
	        clearUiVotingInfo();
	    } else {
	        ui->blsPublic->setText(QString::fromStdString(pubKey));
	        ui->votingKey->setText(
		    QString::fromStdString(CBitcoinAddress(mn->pdmnState->keyIDVoting).ToString()));
	        ui->nodeId->setText(
		    QString::fromStdString(mn->pdmnState->Identity));
	        ui->protxStatus->setText(QString("Registered"));
	    }
    }
    QString qNodeStatus = QString::fromStdString(nodeStatus);
    if (qNodeStatus.size() > 96) {
	    int breakline = qNodeStatus.indexOf('.');
	    while (breakline != -1) {
	        qNodeStatus.insert(breakline + 1, '\n');
	        breakline = qNodeStatus.indexOf('.', breakline+1);
	    }
    }
    ui->voterNodeStatus->setText(qNodeStatus);
    ui->nodeId->setDisabled(true);
    if (!fMasternodeMode) {
        populateAdditionalInfo(3, QString::fromStdString(strPrivKey));
    }
}

void RPCConsole::fetchVotingNodeInfo()
{
    if (votingNodeInfo.regStatus) {
	    ui->collateralAddress->setText(votingNodeInfo.collateralAddress);
	    ui->ownerKey->setText(QString::fromStdString(votingNodeInfo.ownerKeyAddr));
	    ui->votingKey->setText(QString::fromStdString(votingNodeInfo.votingAddress));
	    ui->blsSecret->setText(QString::fromStdString(votingNodeInfo.blsPrivate));
	    ui->nodeId->setText(QString::fromStdString(votingNodeInfo.identity));
	    ui->collateralHash->setText(votingNodeInfo.collateralHash);
	    ui->protxStatus->setText(QString("OK"));
	    ui->voterNodeStatus->setText(QString(tr("Registered")));
    } else {
        populateAdditionalInfo(4, "");
	    fetchMasternodeInfo();
	    fetchCollateralAddress();
    }
}
    
void RPCConsole::setupVotingTab()
{
    ui->btn_readinstruct->setDisabled(false);
    if (masternodeSync.IsBlockchainSynced()) {
        populateAdditionalInfo(0, QString::fromStdString(""));
        ui->btn_genvoterkeys->setDisabled(false);
        ui->btn_revokevotingnode->setDisabled(false);
    } else {
        populateAdditionalInfo(-1, QString::fromStdString(""));
        ui->btn_genvoterkeys->setDisabled(true);
        ui->btn_revokevotingnode->setDisabled(true);
    }
}

QString RPCConsole::getNewRecvAddress()
{
    std::string strAddress;
    std::string command = "getnewaddress\n";

    try {
	RPCConsole::RPCExecuteCommandLine(strAddress, command);
    } catch (UniValue &e) {
	return QString();
    }

    return QString::fromStdString(strAddress);
}

void RPCConsole::genBlsKeys(QString &blsPrivate, QString &blsPublic)
{
    const std::string command = "bls generate\n";
    std::string strResult;
    RPCConsole::RPCExecuteCommandLine(strResult, command);
    QString qstrResult = QString::fromStdString(strResult);
    
    QJsonDocument qJsonDoc = QJsonDocument::fromJson(qstrResult.toUtf8());
    if (!qJsonDoc.isNull()) {
	QJsonObject jsonResult = qJsonDoc.object();
	blsPrivate = jsonResult["secret"].toString();
	blsPublic = jsonResult["public"].toString();
    } else {
	blsPrivate = tr("Error");
	blsPublic = tr("Key generation failed");
    }
}

/** Generate needed keys to setup voting node */
void RPCConsole::genVoterKeys()
{
    QString ownerKeyAddr = getNewRecvAddress();
    QString votingAddress = getNewRecvAddress();
    QString feeSourceAddr = getNewRecvAddress();
    QString blsPrivate, blsPublic;
    QString collateralAddress, collateralHash;
    
    genBlsKeys(blsPrivate, blsPublic);
    
    if (votingNodeInfo.collateralAddress.isEmpty()) {
        collateralAddress = getNewRecvAddress();
        votingNodeInfo.collateralAddress = collateralAddress;
    }
    
    ui->collateralAddress->setText(collateralAddress);
    ui->ownerKey->setText(ownerKeyAddr);
    ui->votingKey->setText(votingAddress);
    ui->feeKey->setText(feeSourceAddr);
    ui->blsSecret->setText(blsPrivate);
    ui->blsPublic->setText(blsPublic);

    votingNodeInfo.ownerKeyAddr = ownerKeyAddr.toStdString();
    votingNodeInfo.votingAddress = votingAddress.toStdString();
    votingNodeInfo.feeSourceAddr = feeSourceAddr;
    votingNodeInfo.blsPublic = blsPublic.toStdString();
    
    ui->btn_sendprotx->setToolTip(
	tr("Wait for confirmation of the transaction"));
    ui->btn_genvoterkeys->setDisabled(true);
    ui->btn_sendvotingnodetx->setDisabled(false);
    votingNodeInfo.payoutAddr = getNewRecvAddress().toStdString();
    populateAdditionalInfo(1, QString::fromStdString(""));
}

void sendToFeeSource() {
    std::string feeSourceAddr = votingNodeInfo.feeSourceAddr.toStdString();
    const std::string sendFee = "sendtoaddress " + feeSourceAddr + " 1";
    std::string strResult;
    
    try {
	RPCConsole::RPCExecuteCommandLine(strResult, sendFee);
    } catch (UniValue &e) {
	return;
    }

}

/** Send collateral transaction for voting node  */
void RPCConsole::sendVotingNodeTx()
{
    std::string	collateralAddress;
    std::string sendCollateral;
    std::string strResult;

    QMessageBox noMoney;
    QMessageBox msgBox;
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText(tr("You are going to send 100 HTA to yourself."));
    msgBox.setInformativeText(tr("You will lose a very small transaction fee."));
    msgBox.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);
    int response = msgBox.exec();

    switch (response) {
    case QMessageBox::Ok: {

	sendToFeeSource();
	if (votingNodeInfo.collateralHash.isEmpty())
	    votingNodeInfo.collateralAddress = getNewRecvAddress();
	try {
	    collateralAddress = votingNodeInfo.collateralAddress.toStdString();
	    sendCollateral = "sendtoaddress " + collateralAddress + " 100";
	    RPCConsole::RPCExecuteCommandLine(strResult, sendCollateral);
	    votingNodeInfo.collateralHash = QString::fromStdString(strResult);
	    ui->collateralHash->setText(QString::fromStdString(strResult));
	    QMetaObject::invokeMethod(this, "collateralReady");
	    ui->btn_sendvotingnodetx->setDisabled(true);
        populateAdditionalInfo(2, QString::fromStdString(""));
	} catch (UniValue &e) {
	    noMoney.setIcon(QMessageBox::Critical);
	    std::string message = find_value(e, "message").get_str();
	    noMoney.setText(QString::fromStdString(message));
	    noMoney.exec();
	    return;
	}
	break;
    }
    case QMessageBox::Cancel:
	break;
    }

    ui->collateralHash->setText(votingNodeInfo.collateralHash);
}

// Fill the missing info in votingNodeInfo struct 
void gatherProTXParams(std::string &command)
{
    const std::string pseudoIP="VOTER";
    std::string result;
    std::string mnoutputs = "masternode outputs";
    QJsonDocument qJsonDoc;

    try {
	    RPCConsole::RPCExecuteCommandLine(result, mnoutputs);
	    qJsonDoc = QJsonDocument::fromJson(QString::fromStdString(result).toUtf8());
	    if (!qJsonDoc.isNull()) {
	        QJsonObject jsonResult = qJsonDoc.object();
	        votingNodeInfo.collateralIndex = jsonResult[votingNodeInfo.collateralHash].toString().toStdString();
	    }
    } catch (UniValue &e) {
	    return;
    }
    
    
    command = "protx register_prepare " +
	votingNodeInfo.collateralHash.toStdString() + " " +
	votingNodeInfo.collateralIndex + " " + pseudoIP + " " +
	votingNodeInfo.ownerKeyAddr + " " + votingNodeInfo.blsPublic +
	" " + votingNodeInfo.votingAddress + " " +
	votingNodeInfo.operatorReward + " " + votingNodeInfo.payoutAddr +
	" " + votingNodeInfo.ipfspeerid + " " + votingNodeInfo.identity +
	" " + votingNodeInfo.feeSourceAddr.toStdString();

}

/** Send ProUpRevTx transaction. Puts voting node in PoSe-banned state. */
void RPCConsole::revokeProTx()
{
    std::string strRevReason, result;
    std::string strMasterNodeBLSPrivKey = GetArg("-masternodeblsprivkey", "");
    QString feeSourceAddress = getNewRecvAddress();
    std::string protx_revoke;
    std::string unlock;

    std::string mnoutputs = "masternode outputs";
    QJsonDocument qJsonDoc;

    try {
        RPCConsole::RPCExecuteCommandLine(result, mnoutputs);
        qJsonDoc = QJsonDocument::fromJson(QString::fromStdString(result).toUtf8());
        if (!qJsonDoc.isNull()) {
            QJsonObject jsonResult = qJsonDoc.object();
            votingNodeInfo.collateralIndex = jsonResult[votingNodeInfo.collateralHash].toString().toStdString();
        }
    } catch (UniValue& e) {
        return;
    }

    votingNodeInfo.feeSourceAddr = feeSourceAddress;
    QString revReason;
    const QString dialogTitle = QString(tr("Revocation Reason"));
    QString revText = tr("Please provide a value (0-3) according to the "
			 "reason you terminating the service (optional).\n"
			 "0 Not Specified\n1 Termination of Service\n"
			 "2 Compromised Keys\n3 Change of Keys\n\n");
    bool ok;
    revReason =	QInputDialog::getText(this, dialogTitle, revText,
				      QLineEdit::Normal, QString(), &ok);
    if (ok) {
	(revReason.isEmpty()
	 ? strRevReason = "0"
	 : strRevReason = revReason.toStdString());
	protx_revoke = ("protx revoke " + votingNodeInfo.proTxHash + " "
			+ strMasterNodeBLSPrivKey + " " + strRevReason + " "
			+ votingNodeInfo.feeSourceAddr.toStdString());
    
    //Construct Raw string unlock command, because RPC-Console breaks improperly on standard Json array
    std::string unlockStringRaw = R"#(lockunspent(true,[{\"txid\":\")#" + votingNodeInfo.collateralHash.toStdString() + R"#(\"\,\"vout\":)#" + votingNodeInfo.collateralIndex + R"#(}]))#" ;
    QMessageBox* warningBox = new QMessageBox(this);
    const QString warningText = QString(tr("This will remove your voting node rights. You must manually remove your masternode settings from your wallet configuration file and restart your wallet to move your coins"));
    warningBox->setIcon(QMessageBox::Warning);
    warningBox->setText(warningText);
    warningBox->setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
    warningBox->setDefaultButton(QMessageBox::Cancel);
    int response = warningBox->exec();
    if (response == QMessageBox::Ok) {
        try {
            sendToFeeSource();
            RPCConsole::RPCExecuteCommandLine(result, protx_revoke);
            RPCConsole::RPCExecuteCommandLine(result, unlockStringRaw);
            populateAdditionalInfo(5, "");
        } catch (UniValue& e) {
            return;
        }
	}
    }
    
}

/** Send ProTX to register voting node */
void RPCConsole::sendProTx()
{
    std::string protx_prepare, sign_message, protx_submit, result;
    QJsonDocument qJsonDoc;

    getNodeIdentityFromInput();
    gatherProTXParams(protx_prepare);
    ui->collateralHash->setText(
	votingNodeInfo.collateralHash
	+ "-" + QString::fromStdString(votingNodeInfo.collateralIndex));
    
    try {

	RPCConsole::RPCExecuteCommandLine(result, protx_prepare);
	qJsonDoc =
	    QJsonDocument::fromJson(QString::fromStdString(result).toUtf8());
	if (!qJsonDoc.isNull()) {
	    QJsonObject jsonResult = qJsonDoc.object();
	    votingNodeInfo.tx = jsonResult["tx"].toString().toStdString();
	    votingNodeInfo.protxCollateralAddr = jsonResult["collateralAddress"].toString().toStdString();
	    votingNodeInfo.signMessage = jsonResult["signMessage"].toString().toStdString();
	    
	    sign_message = ("signmessage "
			    + votingNodeInfo.collateralAddress.toStdString()
			    + " " + votingNodeInfo.signMessage);
	    RPCConsole::RPCExecuteCommandLine(result, sign_message);
	    protx_submit = ("protx register_submit " + votingNodeInfo.tx + " "  + result);
	    RPCConsole::RPCExecuteCommandLine(result, protx_submit);

	    ui->nodeId->setDisabled(true);
	}

    } catch (UniValue &e) {
	std::string message = find_value(e, "message").get_str();
	if (message.size() > 96)
	  message.resize(96);
	ui->protxStatus->setText(QString::fromStdString(message));
	return;
    }

    ui->btn_sendprotx->setDisabled(true);
    ui->voterNodeStatus->setText(QString("Registered"));
    ui->protxStatus->setText(QString("OK"));
    populateAdditionalInfo(3, QString::fromStdString(""));
    disconnect(clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)),
	       this, SLOT(collateralReady()));

}

void RPCConsole::proTxReady(std::string caller)
{
    static unsigned short lock2 = BOOST_BINARY(00);
    unsigned short pass = BOOST_BINARY(11);
    caller == "id" ? lock2 |= BOOST_BINARY(01) : lock2 |= BOOST_BINARY(10);
    if (lock2 == pass) {
	    ui->btn_sendprotx->setToolTip(QString());
	    ui->btn_sendprotx->setDisabled(false);
    }
}
	
bool RPCConsole::nodeIdReady()
{
    bool valid;
    if (masternodeSync.IsBlockchainSynced()) {
        if (!ui->nodeId->text().isEmpty()) {
            if (!IsIdentityValid(ui->nodeId->text().toStdString(), 100 * COIN)) {
                ui->labelNodeId->setText(QString("Identity in use"));
                ui->btn_sendprotx->setDisabled(true);
            } else {
                ui->labelNodeId->setText(QString("Identity Passing"));
                proTxReady("id");
                //ui->btn_sendprotx->setDisabled(false);
            }
        } else {
            ui->labelNodeId->setText(QString("Identity (Must be non-empty!)"));
            ui->btn_sendprotx->setDisabled(true);
        }
    }
}

void RPCConsole::collateralReady()
{
    fetchCollateralAddress();

    if (votingNodeInfo.collateralConfirmations > 0 && !votingNodeInfo.blsPublic.empty())
    {
	    proTxReady("co");
    }
}

void RPCConsole::getNodeIdentityFromInput()
{
    votingNodeInfo.identity = ui->nodeId->text().toStdString();
}

void RPCConsole::openDocUrl()
{
    //QMessageBox openSetupDoc;
    const QUrl url = QUrl("https://historia-docs.readthedocs.io/en/latest/", QUrl::TolerantMode);
    //openSetupDoc.setText("Read the instructions for setting up your node");
    //openSetupDoc.exec();

    QDesktopServices::openUrl(url);
}

void RPCConsole::clearUiVotingInfo()
{
    ui->collateralAddress->setText("N/A");
    ui->ownerKey->setText("N/A");
    ui->votingKey->setText("N/A");
    ui->feeKey->setText("N/A");
    ui->blsSecret->setText("N/A");
    ui->blsPublic->setText("N/A");
    ui->nodeId->setText("");
    ui->collateralHash->setText("N/A");
    ui->protxStatus->setText("N/A");
    ui->voterNodeStatus->setText("N/A");
}

void RPCConsole::restoreTabAfterRevoke()
{
    clearUiVotingInfo();
    ui->btn_revokevotingnode->hide();
    ui->btn_genvoterkeys->show();
    ui->btn_genvoterkeys->setDisabled(false);
    ui->btn_sendvotingnodetx->show();
    ui->btn_sendprotx->show();

    setupVotingTab();
}

/** Restart wallet with "-salvagewallet" */
void RPCConsole::walletSalvage()
{
    buildParameterlist(SALVAGEWALLET);
}

/** Restart wallet with "-rescan" */
void RPCConsole::walletRescan()
{
    buildParameterlist(RESCAN);
}

/** Restart wallet with "-zapwallettxes=1" */
void RPCConsole::walletZaptxes1()
{
    buildParameterlist(ZAPTXES1);
}

/** Restart wallet with "-zapwallettxes=2" */
void RPCConsole::walletZaptxes2()
{
    buildParameterlist(ZAPTXES2);
}

/** Restart wallet with "-upgradewallet" */
void RPCConsole::walletUpgrade()
{
    buildParameterlist(UPGRADEWALLET);
}

/** Restart wallet with "-reindex" */
void RPCConsole::walletReindex()
{
    buildParameterlist(REINDEX);
}

/** Build command-line parameter list for restart */
void RPCConsole::buildParameterlist(QString arg)
{
    // Get command-line arguments and remove the application name
    QStringList args = QApplication::arguments();
    args.removeFirst();

    // Remove existing repair-options
    args.removeAll(SALVAGEWALLET);
    args.removeAll(RESCAN);
    args.removeAll(ZAPTXES1);
    args.removeAll(ZAPTXES2);
    args.removeAll(UPGRADEWALLET);
    args.removeAll(REINDEX);
   
    // Append repair parameter to command line.
    args.append(arg);

    // Send command-line arguments to BitcoinGUI::handleRestart()
    Q_EMIT handleRestart(args);
}

void RPCConsole::clear(bool clearHistory)
{
    ui->messagesWidget->clear();
    if(clearHistory)
    {
        history.clear();
        historyPtr = 0;
    }
    ui->lineEdit->clear();
    ui->lineEdit->setFocus();

    // Add smoothly scaled icon images.
    // (when using width/height on an img, Qt uses nearest instead of linear interpolation)
    QString iconPath = ":/icons/" + GUIUtil::getThemeName() + "/";
    QString iconName = "";
    
    for(int i=0; ICON_MAPPING[i].url; ++i)
    {
        iconName = ICON_MAPPING[i].source;
        ui->messagesWidget->document()->addResource(
                    QTextDocument::ImageResource,
                    QUrl(ICON_MAPPING[i].url),
                    QImage(iconPath + iconName).scaled(QSize(consoleFontSize*2, consoleFontSize*2), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }

    // Set default style sheet
    QFontInfo fixedFontInfo(GUIUtil::fixedPitchFont());
    ui->messagesWidget->document()->setDefaultStyleSheet(
        QString(
                "table { }"
                "td.time { color: #808080; font-size: %2; padding-top: 3px; } "
                "td.message { font-family: %1; font-size: %2; white-space:pre-wrap; } "
                "td.cmd-request { color: #006060; } "
                "td.cmd-error { color: red; } "
                ".secwarning { color: red; }"
                "b { color: #006060; } "
            ).arg(fixedFontInfo.family(), QString("%1pt").arg(consoleFontSize))
        );

    message(CMD_REPLY, (tr("Welcome to the %1 RPC console.").arg(tr(PACKAGE_NAME)) + "<br>" +
                        tr("Use up and down arrows to navigate history, and <b>Ctrl-L</b> to clear screen.") + "<br>" +
                        tr("Type <b>help</b> for an overview of available commands.")) +
                        "<br><span class=\"secwarning\">" +
                        tr("WARNING: Scammers have been active, telling users to type commands here, stealing their wallet contents. Do not use this console without fully understanding the ramification of a command.") +
                        "</span>",
                        true);
}

void RPCConsole::keyPressEvent(QKeyEvent *event)
{
    if(windowType() != Qt::Widget && event->key() == Qt::Key_Escape)
    {
        close();
    }
}

void RPCConsole::message(int category, const QString &message, bool html)
{
    QTime time = QTime::currentTime();
    QString timeString = time.toString();
    QString out;
    out += "<table><tr><td class=\"time\" width=\"65\">" + timeString + "</td>";
    out += "<td class=\"icon\" width=\"32\"><img src=\"" + categoryClass(category) + "\"></td>";
    out += "<td class=\"message " + categoryClass(category) + "\" valign=\"middle\">";
    if(html)
        out += message;
    else
        out += GUIUtil::HtmlEscape(message, false);
    out += "</td></tr></table>";
    ui->messagesWidget->append(out);
}

void RPCConsole::updateNetworkState()
{
    QString connections = QString::number(clientModel->getNumConnections()) + " (";
    connections += tr("In:") + " " + QString::number(clientModel->getNumConnections(CONNECTIONS_IN)) + " / ";
    connections += tr("Out:") + " " + QString::number(clientModel->getNumConnections(CONNECTIONS_OUT)) + ")";

    if(!clientModel->getNetworkActive()) {
        connections += " (" + tr("Network activity disabled") + ")";
    }

    ui->numberOfConnections->setText(connections);
}

void RPCConsole::setNumConnections(int count)
{
    if (!clientModel)
        return;

    updateNetworkState();
}

void RPCConsole::setNetworkActive(bool networkActive)
{
    updateNetworkState();
}

void RPCConsole::setNumBlocks(int count, const QDateTime& blockDate, double nVerificationProgress, bool headers)
{
    if (!headers) {
        ui->numberOfBlocks->setText(QString::number(count));
        ui->lastBlockTime->setText(blockDate.toString());
    }
}

void RPCConsole::updateMasternodeCount()
{
    if (!clientModel) {
        return;
    }
    auto mnList = clientModel->getMasternodeList();
    QString strMasternodeCount = tr("Total: %1 (Enabled: %2)")
        .arg(QString::number(mnList.GetAllMNsCount()))
        .arg(QString::number(mnList.GetValidMNsCount()));
    ui->masternodeCount->setText(strMasternodeCount);
}

void RPCConsole::setMempoolSize(long numberOfTxs, size_t dynUsage)
{
    ui->mempoolNumberTxs->setText(QString::number(numberOfTxs));

    if (dynUsage < 1000000)
        ui->mempoolSize->setText(QString::number(dynUsage/1000.0, 'f', 2) + " KB");
    else
        ui->mempoolSize->setText(QString::number(dynUsage/1000000.0, 'f', 2) + " MB");
}

void RPCConsole::setInstantSendLockCount(size_t count)
{
    ui->instantSendLockCount->setText(QString::number(count));
}

void RPCConsole::on_lineEdit_returnPressed()
{
    QString cmd = ui->lineEdit->text();

    if(!cmd.isEmpty())
    {
        std::string strFilteredCmd;
        try {
            std::string dummy;
            if (!RPCParseCommandLine(dummy, cmd.toStdString(), false, &strFilteredCmd)) {
                // Failed to parse command, so we cannot even filter it for the history
                throw std::runtime_error("Invalid command line");
            }
        } catch (const std::exception& e) {
            QMessageBox::critical(this, "Error", QString("Error: ") + QString::fromStdString(e.what()));
            return;
        }

        ui->lineEdit->clear();

        cmdBeforeBrowsing = QString();

        message(CMD_REQUEST, cmd);
        Q_EMIT cmdRequest(cmd);

        cmd = QString::fromStdString(strFilteredCmd);

        // Remove command, if already in history
        history.removeOne(cmd);
        // Append command to history
        history.append(cmd);
        // Enforce maximum history size
        while(history.size() > CONSOLE_HISTORY)
            history.removeFirst();
        // Set pointer to end of history
        historyPtr = history.size();

        // Scroll console view to end
        scrollToEnd();
    }
}

void RPCConsole::browseHistory(int offset)
{
    // store current text when start browsing through the history
    if (historyPtr == history.size()) {
        cmdBeforeBrowsing = ui->lineEdit->text();
    }

    historyPtr += offset;
    if(historyPtr < 0)
        historyPtr = 0;
    if(historyPtr > history.size())
        historyPtr = history.size();
    QString cmd;
    if(historyPtr < history.size())
        cmd = history.at(historyPtr);
    else if (!cmdBeforeBrowsing.isNull()) {
        cmd = cmdBeforeBrowsing;
    }
    ui->lineEdit->setText(cmd);
}

void RPCConsole::startExecutor()
{
    RPCExecutor *executor = new RPCExecutor();
    executor->moveToThread(&thread);

    // Replies from executor object must go to this object
    connect(executor, SIGNAL(reply(int,QString)), this, SLOT(message(int,QString)));
    // Requests from this object must go to executor
    connect(this, SIGNAL(cmdRequest(QString)), executor, SLOT(request(QString)));

    // On stopExecutor signal
    // - quit the Qt event loop in the execution thread
    connect(this, SIGNAL(stopExecutor()), &thread, SLOT(quit()));
    // - queue executor for deletion (in execution thread)
    connect(&thread, SIGNAL(finished()), executor, SLOT(deleteLater()), Qt::DirectConnection);

    // Default implementation of QThread::run() simply spins up an event loop in the thread,
    // which is what we want.
    thread.start();
}

void RPCConsole::on_tabWidget_currentChanged(int index)
{
    if (ui->tabWidget->widget(index) == ui->tab_console)
        ui->lineEdit->setFocus();
    else if (ui->tabWidget->widget(index) == ui->tab_votingnode)
	preSetupVotingTab();
    else if (ui->tabWidget->widget(index) != ui->tab_peers)
        clearSelectedNode();
}

void RPCConsole::on_openDebugLogfileButton_clicked()
{
    GUIUtil::openDebugLogfile();
}

void RPCConsole::on_openConfButton_clicked()
{
    GUIUtil::openConfigfile();
}

void RPCConsole::scrollToEnd()
{
    QScrollBar *scrollbar = ui->messagesWidget->verticalScrollBar();
    scrollbar->setValue(scrollbar->maximum());
}

void RPCConsole::on_sldGraphRange_valueChanged(int value)
{
    setTrafficGraphRange(static_cast<TrafficGraphData::GraphRange>(value));
}

QString RPCConsole::FormatBytes(quint64 bytes)
{
    if(bytes < 1024)
        return QString(tr("%1 B")).arg(bytes);
    if(bytes < 1024 * 1024)
        return QString(tr("%1 KB")).arg(bytes / 1024);
    if(bytes < 1024 * 1024 * 1024)
        return QString(tr("%1 MB")).arg(bytes / 1024 / 1024);

    return QString(tr("%1 GB")).arg(bytes / 1024 / 1024 / 1024);
}

void RPCConsole::setTrafficGraphRange(TrafficGraphData::GraphRange range)
{
    ui->trafficGraph->setGraphRangeMins(range);
    ui->lblGraphRange->setText(GUIUtil::formatDurationStr(TrafficGraphData::RangeMinutes[range] * 60));
}

void RPCConsole::updateTrafficStats(quint64 totalBytesIn, quint64 totalBytesOut)
{
    ui->lblBytesIn->setText(FormatBytes(totalBytesIn));
    ui->lblBytesOut->setText(FormatBytes(totalBytesOut));
}

void RPCConsole::peerSelected(const QItemSelection &selected, const QItemSelection &deselected)
{
    Q_UNUSED(deselected);

    if (!clientModel || !clientModel->getPeerTableModel() || selected.indexes().isEmpty())
        return;

    const CNodeCombinedStats *stats = clientModel->getPeerTableModel()->getNodeStats(selected.indexes().first().row());
    if (stats)
        updateNodeDetail(stats);
}

void RPCConsole::peerLayoutAboutToChange()
{
    QModelIndexList selected = ui->peerWidget->selectionModel()->selectedIndexes();
    cachedNodeids.clear();
    for(int i = 0; i < selected.size(); i++)
    {
        const CNodeCombinedStats *stats = clientModel->getPeerTableModel()->getNodeStats(selected.at(i).row());
        cachedNodeids.append(stats->nodeStats.nodeid);
    }
}

void RPCConsole::peerLayoutChanged()
{
    if (!clientModel || !clientModel->getPeerTableModel())
        return;

    const CNodeCombinedStats *stats = NULL;
    bool fUnselect = false;
    bool fReselect = false;

    if (cachedNodeids.empty()) // no node selected yet
        return;

    // find the currently selected row
    int selectedRow = -1;
    QModelIndexList selectedModelIndex = ui->peerWidget->selectionModel()->selectedIndexes();
    if (!selectedModelIndex.isEmpty()) {
        selectedRow = selectedModelIndex.first().row();
    }

    // check if our detail node has a row in the table (it may not necessarily
    // be at selectedRow since its position can change after a layout change)
    int detailNodeRow = clientModel->getPeerTableModel()->getRowByNodeId(cachedNodeids.first());

    if (detailNodeRow < 0)
    {
        // detail node disappeared from table (node disconnected)
        fUnselect = true;
    }
    else
    {
        if (detailNodeRow != selectedRow)
        {
            // detail node moved position
            fUnselect = true;
            fReselect = true;
        }

        // get fresh stats on the detail node.
        stats = clientModel->getPeerTableModel()->getNodeStats(detailNodeRow);
    }

    if (fUnselect && selectedRow >= 0) {
        clearSelectedNode();
    }

    if (fReselect)
    {
        for(int i = 0; i < cachedNodeids.size(); i++)
        {
            ui->peerWidget->selectRow(clientModel->getPeerTableModel()->getRowByNodeId(cachedNodeids.at(i)));
        }
    }

    if (stats)
        updateNodeDetail(stats);
}

void RPCConsole::updateNodeDetail(const CNodeCombinedStats *stats)
{
    // update the detail ui with latest node information
    QString peerAddrDetails(QString::fromStdString(stats->nodeStats.addrName) + " ");
    peerAddrDetails += tr("(node id: %1)").arg(QString::number(stats->nodeStats.nodeid));
    if (!stats->nodeStats.addrLocal.empty())
        peerAddrDetails += "<br />" + tr("via %1").arg(QString::fromStdString(stats->nodeStats.addrLocal));
    ui->peerHeading->setText(peerAddrDetails);
    ui->peerServices->setText(GUIUtil::formatServicesStr(stats->nodeStats.nServices));
    ui->peerLastSend->setText(stats->nodeStats.nLastSend ? GUIUtil::formatDurationStr(GetSystemTimeInSeconds() - stats->nodeStats.nLastSend) : tr("never"));
    ui->peerLastRecv->setText(stats->nodeStats.nLastRecv ? GUIUtil::formatDurationStr(GetSystemTimeInSeconds() - stats->nodeStats.nLastRecv) : tr("never"));
    ui->peerBytesSent->setText(FormatBytes(stats->nodeStats.nSendBytes));
    ui->peerBytesRecv->setText(FormatBytes(stats->nodeStats.nRecvBytes));
    ui->peerConnTime->setText(GUIUtil::formatDurationStr(GetSystemTimeInSeconds() - stats->nodeStats.nTimeConnected));
    ui->peerPingTime->setText(GUIUtil::formatPingTime(stats->nodeStats.dPingTime));
    ui->peerPingWait->setText(GUIUtil::formatPingTime(stats->nodeStats.dPingWait));
    ui->peerMinPing->setText(GUIUtil::formatPingTime(stats->nodeStats.dMinPing));
    ui->timeoffset->setText(GUIUtil::formatTimeOffset(stats->nodeStats.nTimeOffset));
    ui->peerVersion->setText(QString("%1").arg(QString::number(stats->nodeStats.nVersion)));
    ui->peerSubversion->setText(QString::fromStdString(stats->nodeStats.cleanSubVer));
    ui->peerDirection->setText(stats->nodeStats.fInbound ? tr("Inbound") : tr("Outbound"));
    ui->peerHeight->setText(QString("%1").arg(QString::number(stats->nodeStats.nStartingHeight)));
    ui->peerWhitelisted->setText(stats->nodeStats.fWhitelisted ? tr("Yes") : tr("No"));

    // This check fails for example if the lock was busy and
    // nodeStateStats couldn't be fetched.
    if (stats->fNodeStateStatsAvailable) {
        // Ban score is init to 0
        ui->peerBanScore->setText(QString("%1").arg(stats->nodeStateStats.nMisbehavior));

        // Sync height is init to -1
        if (stats->nodeStateStats.nSyncHeight > -1)
            ui->peerSyncHeight->setText(QString("%1").arg(stats->nodeStateStats.nSyncHeight));
        else
            ui->peerSyncHeight->setText(tr("Unknown"));

        // Common height is init to -1
        if (stats->nodeStateStats.nCommonHeight > -1)
            ui->peerCommonHeight->setText(QString("%1").arg(stats->nodeStateStats.nCommonHeight));
        else
            ui->peerCommonHeight->setText(tr("Unknown"));
    }

    ui->detailWidget->show();
}

void RPCConsole::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

void RPCConsole::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

    if (!clientModel || !clientModel->getPeerTableModel())
        return;

    // start PeerTableModel auto refresh
    clientModel->getPeerTableModel()->startAutoRefresh();
}

void RPCConsole::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);

    if (!clientModel || !clientModel->getPeerTableModel())
        return;

    // stop PeerTableModel auto refresh
    clientModel->getPeerTableModel()->stopAutoRefresh();
}

void RPCConsole::showPeersTableContextMenu(const QPoint& point)
{
    QModelIndex index = ui->peerWidget->indexAt(point);
    if (index.isValid())
        peersTableContextMenu->exec(QCursor::pos());
}

void RPCConsole::showBanTableContextMenu(const QPoint& point)
{
    QModelIndex index = ui->banlistWidget->indexAt(point);
    if (index.isValid())
        banTableContextMenu->exec(QCursor::pos());
}

void RPCConsole::disconnectSelectedNode()
{
    if(!g_connman)
        return;
    
    // Get selected peer addresses
    QList<QModelIndex> nodes = GUIUtil::getEntryData(ui->peerWidget, PeerTableModel::NetNodeId);
    for(int i = 0; i < nodes.count(); i++)
    {
        // Get currently selected peer address
        NodeId id = nodes.at(i).data().toLongLong();
        // Find the node, disconnect it and clear the selected node
        if(g_connman->DisconnectNode(id))
            clearSelectedNode();
    }
}

void RPCConsole::banSelectedNode(int bantime)
{
    if (!clientModel || !g_connman)
        return;
    
    // Get selected peer addresses
    QList<QModelIndex> nodes = GUIUtil::getEntryData(ui->peerWidget, PeerTableModel::NetNodeId);
    for(int i = 0; i < nodes.count(); i++)
    {
        // Get currently selected peer address
        NodeId id = nodes.at(i).data().toLongLong();

	// Get currently selected peer address
	int detailNodeRow = clientModel->getPeerTableModel()->getRowByNodeId(id);
	if(detailNodeRow < 0)
	    return;

	// Find possible nodes, ban it and clear the selected node
	const CNodeCombinedStats *stats = clientModel->getPeerTableModel()->getNodeStats(detailNodeRow);
	if(stats) {
	    g_connman->Ban(stats->nodeStats.addr, BanReasonManuallyAdded, bantime);
	}
    }
    clearSelectedNode();
    clientModel->getBanTableModel()->refresh();
}

void RPCConsole::unbanSelectedNode()
{
    if (!clientModel)
        return;

    // Get selected ban addresses
    QList<QModelIndex> nodes = GUIUtil::getEntryData(ui->banlistWidget, BanTableModel::Address);
    for(int i = 0; i < nodes.count(); i++)
    {
        // Get currently selected ban address
        QString strNode = nodes.at(i).data().toString();
        CSubNet possibleSubnet;

        LookupSubNet(strNode.toStdString().c_str(), possibleSubnet);
        if (possibleSubnet.IsValid() && g_connman)
        {
            g_connman->Unban(possibleSubnet);
            clientModel->getBanTableModel()->refresh();
        }
    }
}

void RPCConsole::clearSelectedNode()
{
    ui->peerWidget->selectionModel()->clearSelection();
    cachedNodeids.clear();
    ui->detailWidget->hide();
    ui->peerHeading->setText(tr("Select a peer to view detailed information."));
}

void RPCConsole::showOrHideBanTableIfRequired()
{
    if (!clientModel)
        return;

    bool visible = clientModel->getBanTableModel()->shouldShow();
    ui->banlistWidget->setVisible(visible);
    ui->banHeading->setVisible(visible);
}

void RPCConsole::setTabFocus(enum TabTypes tabType)
{
    ui->tabWidget->setCurrentIndex(tabType);
}

