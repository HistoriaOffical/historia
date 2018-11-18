Historia Masternode Setup - Windows 10
----------------
## Setup ##
Setting up a masternode requires a basic understanding of Windows and blockchain technology, as well as an ability to follow instructions closely. It also requires regular maintenance and careful security. There are some decisions to be made along the way, and optional extra steps to take for increased security.


### Before you begin ###

It should be noted in that this is a basic guide for Windows that does not take into account, security. The following is a hot wallet setup and is not recommended because of security issues. This is basic guide, please use at your own risk.

Please update your wallet - https://github.com/HistoriaOffical/historia/releases/tag/0.16.2.0 

This guide assumes you are setting up a single masternode for the first time. You will need:

* 100 Historia
* A wallet to store your Historia, masternodes MUST use 0.16.2.0
* A Windows 10 instance.
* Since we are assume this is home network, TCP Port 10101 and 4001 need to be publicly open via a port forward to the Internet.


## Install Historia Windows Wallet ##
You MUST use Historia 0.16.2.0, otherwise this process will fail. https://github.com/HistoriaOffical/historia/releases/tag/0.16.2.0 

Download the correct Windows Historia setup file from the previous URL. Once downloaded, run the Historia installer and install the Historia wallet. Open the wallet and let the blockchain sync completely.

### Send the collateral ###


A Historia address with a single unspent transaction output (UTXO) of exactly 100 HISTORIA is required to operate a masternode. Once it has been sent, various keys regarding the transaction must be extracted for later entry in a configuration file as proof that the transaction was completed successfully. A masternode can only be started from a official Historia Core wallet at this time. This guide will describe the steps for Historia Core wallet.


#### Sending from Historia Core wallet ####
Open Historia Core wallet and wait for it to synchronize with the network. It should look like this when ready:

![picture alt](https://github.com/HistoriaOffical/historia/blob/master/historia-docs/masternode/Picture10.png)
Fully synchronized Historia Core wallet


Click Tools > Debug console to open the console. Type the following two commands into the console to generate a masternode key and get a fresh address:


> masternode genkey  
> getaccountaddress 0  

![picture alt](https://github.com/HistoriaOffical/historia/blob/master/historia-docs/masternode/Picture11.png)

Generating a masternode private key in Historia Core wallet
Take note of the masternode private key and collateral address, since we will need it later. The next step is to secure your wallet (if you have not already done so). First, encrypt the wallet by selecting Settings > Encrypt wallet. You should use a strong, new password that you have never used somewhere else. Take note of your password and store it somewhere safe or you will be permanently locked out of your wallet and lose access to your funds. Next, back up your wallet file by selecting File > Backup Wallet. Save the file to a secure location physically separate to your computer, since this will be the only way you can access our funds if anything happens to your computer.


Now send exactly 100 HISTORIA in a single transaction to the account address you generated in the previous step. This may be sent from another wallet, or from funds already held in your current wallet. Once the transaction is complete, view the transaction in a blockchain explorer by searching for the address. You will need 15 confirmations before you can start the masternode, but you can continue with the next step at this point already: installing Historia Core on your VPS.

Next, setup the historia.conf files by selecting Tools > Open Wallet Configuration File.

A text editor window will appear. We now need to create a configuration file specifying several variables. Copy and paste the following text into the Wallet Configuration file, then replace the variables specific to your configuration as follows:

> #----  
> rpcuser=XXXXXXXXXXXXX  
> rpcpassword=XXXXXXXXXXXXXXXXXXXXXXXXXXXX  
> rpcallowip=127.0.0.1  
> #----  
> listen=1  
> server=1  
> daemon=1  
> maxconnections=64  
> #----  
> masternode=1  
> masternodeprivkey=XXXXXXXXXXXXXXXXXXXXXXX  
> externalip=XXX.XXX.XXX.XXX:10101  
> #----  


Replace the fields marked with XXXXXXX as follows:
* rpcuser: enter any string of numbers or letters, no special characters allowed
* rpcpassword: enter any string of numbers or letters, no special characters allowed
* masternodeprivkey: this is the private key you generated in the previous step
* externalip: this is the IP address of your internet connection with the Historia port

Save the historia.conf file in the default location (C:\Users\yourusername\AppData\Roaming\HistoriaCore\) and exit the text editor.

Next let's install IPFS.

## IPFS ##
Running the IPFS daemon is now a required part of the masternode system. You will not be able to run a masternode unless you complete the following steps.

#### Download / Install IPFS Daemon ####

Download the Windows zip file from https://dist.ipfs.io/#go-ipfs

Extract the zip file and copy the ipfs.exe files to your HistoriaCore directory (Default location: C:\Users\yourusername\AppData\Roaming\HistoriaCore\)

#### Initialize IPFS Daemon for Historia ####
Since we will be using IPFS only for Historia, we can safely store the ipfs.exe file in the HistoriaCore directory and initalize IPFS.

> cd C:\Users\yourusername\AppData\Roaming\HistoriaCore\
> ipfs.exe init

#### Start IPFS Daemon for Historia ####
Before you start your masternode, IPFS daemon must be running. 

> ipfs.exe daemon

There is a better way to do this by adding a service but we havent gotten there yet. 

*If you reboot your Windows Machine, you now must restart both Historiad and ipfs daemon*

For additional information:
https://docs.ipfs.io/introduction/install/

Next lets start Historiad

## Install Sentinel ##

Download and install Sentinel for Windows
https://github.com/HistoriaOffical/sentinel/releases

Create new sentinel directory in your HistoraCore directory

And copy sentinel.exe to the newly created sentinel directory
C:\Users\yourusername\AppData\Roaming\HistoriaCore\sentinel\sentinel.exe

Create new file in the sentinel directory named sentinel.conf

C:\Users\yourusername\AppData\Roaming\HistoriaCore\sentinel\sentinel.conf

Edit file and paste the following into the sentinel.conf file.
>  
>network=mainnet  
>db_name=database/sentinel.db  
>db_driver=sqlite



### Setup Task for Sentinel ###

Run Task Scheduler  

Create Task  

General Tab - Name: Sentinal  

Trigger Tab -> New (Trigger)  

Settings -> Repeat Daily  
Recur Every: 1 day  
Advanced Settings:  
Repeat Task Every: 1 Minute (Notice you have to select 5 minutes from the drop down, then edit the 5 to 1)  
For a duration of:Indefinitely  

Actions Tab -> New (Action)  
Program/script -> Browse to C:\Users\yourusername\AppData\Roaming\HistoriaCore\sentinel\sentinel.exe  
Click Ok  

Conditions Tab -> Power  
Uncheck box for "Start task only if the computer is on AC Power"  
Click Ok  



## Start your masternode ##
Depending on how you sent your masternode collateral, you will need to start your masternode with a command sent by the Historia Core wallet. Before you continue, you must ensure that your 100 HISTORIA collateral transaction has at least 15 confirmation, and that historiad is running and fully synchronized with the blockchain on your masternode. See the previous step for details on how to do this. During the startup process, your masternode may pass through the following states:
*	MASTERNODE_SYNC: This indicates the data currently being synchronised in the masternode
*	MASTERNODE_SYNC_FAILED: Synchronisation could not complete, check your firewall and restart historiad
*	WATCHDOG_EXPIRED: Waiting for sentinel to restart, make sure it is entered in crontab
*	NEW_START_REQUIRED: Start command must be sent from wallet
*	PRE_ENABLED: Waiting for network to recognize started masternode
*	ENABLED: Masternode successfully started
If you masternode does not seem to start immediately, do not arbitrarily issue more start commands. Each time you do so, you will reset your position in the payment queue.

#### Starting from Historia Core wallet ####
If you used an address in Historia Core wallet for your collateral transaction, you now need to find the txid of the transaction. Click Tools > Debug console and enter the following command:

> masternode outputs

This should return a string of characters similar to this:
> {  
> "06e38868bb8f9958e34d5155437d009b72dff33fc28874c87fd42e51c0f74fdb" : "0",  
> }

The first long string is your transaction hash, while the last number is the index. We now need open Tool -> Open Masternode Configure file for this wallet in order to be able to use it to issue the command to start your masternode on the network. 


Open a new text file in Notepad (or TextEdit on macOS, nano on Linux) and enter the following information:
*	Label: Any single word used to identify your masternode, e.g. MN1
*	IP and port: The IP address and port (usually 10101) configured in the Historia.conf file, separated by a colon (:)
*	Masternode private key: This is the result of your masternode genkey command earlier, also the same as configured in the Historia.conf file
*	Transaction hash: The txid we just identified using masternode outputs
*	Index: The index we just identified using masternode outputs


Enter all of this information on a single line with each item separated by a space, for example:


> MN1 52.14.2.67:10101 XrxSr3fXpX3dZcU7CoiFuFWqeHYw83r28btCFfIHqf6zkMp1PZ4 06e38868bb8f9958e34d5155437d009b72dff33fc28874c87fd42e51c0f74fdb 0

*Notice: The above line looks like it's on 2 lines, but this should be all on one line*

Save this file and close the text editor. It should be saved in the C:\Users\yourusername\AppData\Roaming\HistoriaCore folder.

Platform  | Path | Shortcut
------------- | ------------- | -------------
Windows  | C:\Users\yourusername\AppData\Roaming\HistoriaCore | %APPDATA%\HistoriaCore


Shut down and restart Historia Core wallet. Let the Historia Core wallet fully sync. Historia Core will recognize masternode.conf during startup, and is now ready to activate your masternode. Go to Settings > Unlock Wallet and enter your wallet passphrase. Then click Tools > Debug console again and enter the following command to start your masternode (replace MN1 with the label for your masternode):


> masternode start-alias MN1


At this point you can go back to your terminal window and monitor your masternode by entering ~/.Historiacore/historia-cli masternode status. You will probably need to wait around 30 minutes as the node passes through the PRE_ENABLED stage and finally reaches ENABLED. Give it some time.
At this point you can safely log out of your server by typing exit. Congratulations! Your masternode is now running.

