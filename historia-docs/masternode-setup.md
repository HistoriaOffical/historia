

Historia Masternode Setup
----------------
### Setup ###


Setting up a masternode requires a basic understanding of Linux and blockchain technology, as well as an ability to follow instructions closely. It also requires regular maintenance and careful security. There are some decisions to be made along the way, and optional extra steps to take for increased security.
### Before you begin ###

Please update your wallet - https://github.com/HistoriaOffical/historia/releases/tag/0.16.1.0 

This guide assumes you are setting up a single masternode for the first time. You will need:

* 100 Historia
* A wallet to store your Historia, masternodes MUST use 0.16.1
* A Linux server, preferably a Virtual Private Server (VPS)


We also assume you will be working from a Windows computer. However, since most of the work is done on your Linux VPS, alternative steps for using macOS or Linux will be indicated where necessary.

### Set up your VPS ###
A VPS, more commonly known as a cloud server, is fully functional installation of an operating system (usually Linux) operating within a virtual machine. The virtual machine allows the VPS provider to run multiple systems on one physical server, making it more efficient and much cheaper than having a single operating system running on the “bare metal” of each server. A VPS is ideal for hosting a Historia masternode because they typically offer guaranteed uptime, redundancy in the case of hardware failure and a static IP address that is required to ensure you remain in the masternode payment queue. While running a masternode from home on a desktop computer is technically possible, it will most likely not work reliably because most ISPs allocate dynamic IP addresses to home users.

We will use Vultr hosting as an example of a VPS. First create an account and add credit. Then go to the Servers menu item on the left and click + to add a new server. Select a location for your new server on the following screen:

![picture alt](https://github.com/HistoriaOffical/historia/blob/master/historia-docs/masternode/Picture1.png)
Vultr server location selection screen


Select Ubuntu 16.04 x64 as the server type. We use 16.04 instead of the latest version because 16.04 is an LTS release of Ubuntu, which will be supported with security updates for 5 years instead of the usual 9 months.

![picture alt](https://github.com/HistoriaOffical/historia/blob/master/historia-docs/masternode/Picture2.png)
Vultr server type selection screen


To be safe, select a server size offering at least 2GB of memory. 1 GB should be possible currently, but once the IPFS Masternode updates are integrated, 2 GB needed. 

![picture alt](https://github.com/HistoriaOffical/historia/blob/master/historia-docs/masternode/Picture3.png)
Vultr server size selection screen

Enter a hostname and label for your server. In this example we will use htamn01 as the hostname.
![picture alt](https://github.com/HistoriaOffical/historia/blob/master/historia-docs/masternode/Picture4.png)
Vultr server hostname & label selection screen


Vultr will now install your server. This process may take a few minutes.

![picture alt](https://github.com/HistoriaOffical/historia/blob/master/historia-docs/masternode/Picture5.png)
Vultr server installation screen
Click Manage when installation is complete and take note of the IP address, username and password.

![picture alt](https://github.com/HistoriaOffical/historia/blob/master/historia-docs/masternode/Picture6.png)

Vultr server management screen

### Set up your operating system ###
We will begin by connecting to your newly provisioned server. On Windows, we will first download an app called PuTTY to connect to the server. Go to the PuTTY download page and select the appropriate MSI installer for your system. On Mac or Linux you can ssh directly from the terminal - simply type ssh root@<server_ip> and enter your password when prompted.

![picture alt](https://github.com/HistoriaOffical/historia/blob/master/historia-docs/masternode/Picture7.png)
PuTTY download page


Double-click the downloaded file to install PuTTY, then run the app from your Start menu. Enter the IP address of the server in the Host Name field and click Open. You may see a certificate warning, since this is the first time you are connecting to this server. You can safely click Yes to trust this server in the future.

![picture alt](https://github.com/HistoriaOffical/historia/blob/master/historia-docs/masternode/Picture8.png)
PuTTY security alert when connecting to a new server


You are now connected to your server and should see a terminal window. Begin by logging in to your server with the user root and password supplied by your hosting provider.

![picture alt](https://github.com/HistoriaOffical/historia/blob/master/historia-docs/masternode/Picture9.png)
Password challenge when connecting to your VPS for the first time


You should immediately change the root password and store it in a safe place for security. You can copy and paste any of the following commands by selecting them in your browser, pressing Ctrl + C, then switching to the PuTTY window and right-clicking in the window. The text will paste at the current cursor location:

> passwd root


Enter and confirm a new password (preferably long and randomly generated). Next we will create a new user with the following command, replacing <username> with a username of your choice:


> adduser <username>


You will be prompted for a password. Enter and confirm using a new password (different to your root password) and store it in a safe place. You will also see prompts for user information, but this can be left blank. Once the user has been created, we will add them to the sudo group so they can perform commands as root:


> usermod -aG sudo <username>


Now, while still as root, we will update the system from the Ubuntu package repository:


> apt update  
> apt upgrade


The system will show a list of upgradable packages. Press Y and Enter to install the packages. We will now install a firewall (and some other packages we will use later), add swap memory and reboot the server to apply any necessary kernel updates, and then login to our newly secured environment as the new user:

> apt install ufw python virtualenv git unzip pv


(press Y and Enter to confirm)

> ufw allow ssh/tcp  
> ufw limit ssh/tcp  
> ufw allow 10101/tcp  
> ufw logging on  
> ufw enable


(press Y and Enter to confirm)
> fallocate -l 4G /swapfile  
> chmod 600 /swapfile  
> mkswap /swapfile  
> swapon /swapfile  
> nano /etc/fstab  


Add the following line at the end of the file (press tab to separate each word/number), then press Ctrl + X to close the editor, then Y and Enter save the file.


> /swapfile none swap sw 0 0


Finally, in order to prevent brute force password hacking attacks, open the SSH configuration file to disable root login over SSH:


> nano /etc/ssh/sshd_config


Locate the line that reads PermitRootLogin yes and set it to PermitRootLogin no. Directly below this, add a line which reads AllowUsers <username>, replacing <username> with the username you selected above. The press Ctrl + X to close the editor, then Y and Enter save the file.

Then reboot the server:


> reboot now


PuTTY will disconnect when the server reboots.


While this setup includes basic steps to protect your server against attacks, much more can be done. However, since the masternode does not actually store the keys to any Historia, these steps are considered beyond the scope of this guide.

### Send the collateral ###
You MUST use Historia 0.16.1, otherwise this process will fail. https://github.com/HistoriaOffical/historia/releases/tag/0.16.1.0 


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
### Install Historia Core ###

You MUST use Historia 0.16.1, otherwise this process will fail. https://github.com/HistoriaOffical/historia/releases/tag/0.16.1.0


Historia Core is the software behind both the Historia Core GUI wallet and Historia masternodes. If not displaying a GUI, it runs as a daemon on your VPS (Historiad), controlled by a simple command interface (Historia-cli).
Open PuTTY or a console again and connect using the username and password you just created for your new, non-root user. 


#### Manual installation ####
To manually download and install the components of your Historia masternode, visit https://github.com/HistoriaOffical/historia/releases/tag/0.16.1.0 on your computer to find the link to the latest Historia Core wallet.  Right-click on Download TGZ for Historia Core Linux 64 Bit and select Copy link address. Go back to your terminal window and enter the following command, pasting in the address to the latest version of Historia Core by right clicking or pressing Ctrl + V:

> cd ~  
> wget 
https://github.com/HistoriaOffical/historia/releases/download/0.16.1.0/historiacore-0.16.1-linux64.tar.gz


Create a working directory for Historia, extract the compressed archive, copy the necessary files to the directory and set them as executable:
> mkdir .historiacore  
> tar xfvz historiacore-0.16.1-linux64.tar.gz  
> cp historiacore-0.16.1/bin/historiad .historiacore/  
> cp historiacore-0.16.1/bin/historia-cli .historiacore/  
> chmod 777 .historiacore/historia*  


Clean up unneeded files:
> rm historiacore-0.16.1-linux64.tar.gz  
> rm -r historiacore-0.16.1/


Create a configuration file using the following command:
> nano ~/.historiacore/historia.conf


An editor window will appear. We now need to create a configuration file specifying several variables. Copy and paste the following text to get started, then replace the variables specific to your configuration as follows:

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
> externalip=XXX.XXX.XXX.XXX  
> #----  


Replace the fields marked with XXXXXXX as follows:
* rpcuser: enter any string of numbers or letters, no special characters allowed
* rpcpassword: enter any string of numbers or letters, no special characters allowed
* masternodeprivkey: this is the private key you generated in the previous step
* externalip: this is the IP address of your VPS

The result should look something like this:
![picture alt](https://github.com/HistoriaOffical/historia/blob/master/historia-docs/masternode/Picture12.png)

Entering key data in Historia.conf on the masternode
Press Ctrl + X to close the editor and Y and Enter save the file. You can now start running Historia on the masternode to begin synchronization with the blockchain:
>  ~/.historiacore/historiad


You will see a message reading Historia Core server starting. We will now install Sentinel, a piece of software which operates as a watchdog to communicate to the network that your node is working properly:


> cd ~/.historiacore  
> git clone https://github.com/HistoriaOffical/sentinel.git  
> cd sentinel  
> virtualenv venv  
> venv/bin/pip install -r requirements.txt    
> venv/bin/python bin/sentinel.py

You will see a message reading historiad not synced with network! Awaiting full sync before running Sentinel. Add historiad and sentinel to crontab to make sure it runs every minute to check on your masternode:

> crontab -e

Choose nano as your editor and enter the following lines at the end of the file:

> \* \* \* \* \* cd ~/.historiacore/sentinel && ./venv/bin/python bin/sentinel.py 2>&1 >> sentinel-cron.log  
> \* \* \* \* \* pidof historiad || ~/.historiacore/historiad

Press enter to make sure there is a blank line at the end of the file, then press Ctrl + X to close the editor and Y and Enter save the file. We now need to wait for 15 confirmations of the collateral transaction to complete, and wait for the blockchain to finish synchronizing on the masternode. You can use the following commands to monitor progress:

> ~/.historiacore/historia-cli mnsync status

When synchronisation is complete, you should see the following response:
> {  
>  "AssetID": 999,  
>  "AssetName": "MASTERNODE_SYNC_FINISHED",  
>  "Attempt": 0,  
>  "IsBlockchainSynced": true,  
>  "IsMasternodeListSynced": true,  
>  "IsWinnersListSynced": true,  
>  "IsSynced": true,  
>  "IsFailed": false  
> }  

Continue with the next step to start your masternode.
### Start your masternode ###
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

The first long string is your transaction hash, while the last number is the index. We now need to create a file called masternode.conf for this wallet in order to be able to use it to issue the command to start your masternode on the network. 


Open a new text file in Notepad (or TextEdit on macOS, nano on Linux) and enter the following information:
*	Label: Any single word used to identify your masternode, e.g. MN1
*	IP and port: The IP address and port (usually 10101) configured in the Historia.conf file, separated by a colon (:)
*	Masternode private key: This is the result of your masternode genkey command earlier, also the same as configured in the Historia.conf file
*	Transaction hash: The txid we just identified using masternode outputs
*	Index: The index we just identified using masternode outputs


Enter all of this information on a single line with each item separated by a space, for example:


> MN1 52.14.2.67:10101 XrxSr3fXpX3dZcU7CoiFuFWqeHYw83r28btCFfIHqf6zkMp1PZ4 06e38868bb8f9958e34d5155437d009b72dff33fc28874c87fd42e51c0f74fdb 0

*Notice: The above line looks like it's on 2 lines, but this should be all on one line*

Save this file in the historiacore data folder on the PC running the Historia Core wallet using the filename masternode.conf. You may need to enable View hidden items to view this folder. Be sure to select All files if using Notepad so you don’t end up with a .conf.txt file extension by mistake. For different operating systems, the Historiacore folder can be found in the following locations (copy and paste the shortcut text into the Save dialog to find it quickly):


Platform  | Path | Shortcut
------------- | ------------- | -------------
Linux  | /home/yourusername/.historiacore | ~/.historiacore
OSX  | /Macintosh HD/Library/Application Support/HistoriaCore | ~/Library/Application Support/HistoriaCore
Windows  | C:\Users\yourusername\AppData\Roaming\Historia Core | %APPDATA%\Historia Core



Now close your text editor and also shut down and restart Historia Core wallet. Historia Core will recognize masternode.conf during startup, and is now ready to activate your masternode. Go to Settings > Unlock Wallet and enter your wallet passphrase. Then click Tools > Debug console again and enter the following command to start your masternode (replace MN1 with the label for your masternode):


> masternode start-alias MN1


At this point you can go back to your terminal window and monitor your masternode by entering ~/.Historiacore/historia-cli masternode status. You will probably need to wait around 30 minutes as the node passes through the PRE_ENABLED stage and finally reaches ENABLED. Give it some time.
At this point you can safely log out of your server by typing exit. Congratulations! Your masternode is now running.


### IPFS ###
Currently IPFS is not required for this release. However for future releases, the IPFS daemon will be required. We recommend that users get familiar with how to install the IPFS daemon. The Historia Team will update this document with IPFS install instructions at a later date.
https://docs.ipfs.io/introduction/install/


