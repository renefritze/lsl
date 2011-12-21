class StringMap;
class StringVector;

class ServerEvents;

class iServer : public ChannelList, public UserList, public BattleList
{
  public:
	friend class ServerEvents;

	// Server interface

	virtual bool ExecuteSayCommand( const std::string& cmd ) = 0;

	virtual bool Register( const std::string& addr, const int port, const std::string& nick, const std::string& password,std::string& reason ) = 0;
	virtual void AcceptAgreement() = 0;

	virtual void Connect( const std::string& servername, const std::string& addr, const int port ) = 0;
	virtual void Disconnect() = 0;
	virtual bool IsConnected() = 0;

	virtual void Login() = 0;
	virtual void Logout() = 0;
	virtual bool IsOnline()  const = 0;

	virtual void TimerUpdate();

	virtual void JoinChannel( const std::string& channel, const std::string& key ) = 0;
	virtual void PartChannel( Channel* channel ) = 0;

	virtual void DoActionChannel( const Channel* channel, const std::string& msg ) = 0;
	virtual void SayChannel( const Channel* channel, const std::string& msg ) = 0;

	virtual void SayPrivate( const User* user, const std::string& msg ) = 0;
	virtual void DoActionPrivate( const User* user, const std::string& msg ) = 0;

	virtual void SayBattle( const Battle* battle, const std::string& msg ) = 0;
	virtual void DoActionBattle( const Battle* battle, const std::string& msg ) = 0;

	virtual void Ring( const User* user ) = 0;

	// these need to not use specific classes since they can be nonexistent/offline
	virtual void ModeratorSetChannelTopic( const std::string& channel, const std::string& topic ) = 0;
	virtual void ModeratorSetChannelKey( const std::string& channel, const std::string& key ) = 0;
	virtual void ModeratorMute( const std::string& channel, const std::string& nick, int duration, bool byip ) = 0;
	virtual void ModeratorUnmute( const std::string& channel, const std::string& nick ) = 0;
	virtual void ModeratorKick( const std::string& channel, const std::string& reason ) = 0;
	virtual void ModeratorBan( const std::string& nick, bool byip ) = 0;
	virtual void ModeratorUnban( const std::string& nick ) = 0;
	virtual void ModeratorGetIP( const std::string& nick ) = 0;
	virtual void ModeratorGetLastLogin( const std::string& nick ) = 0;
	virtual void ModeratorGetLastIP( const std::string& nick ) = 0;
	virtual void ModeratorFindByIP( const std::string& ipadress ) = 0;

	virtual void AdminGetAccountAccess( const std::string& nick ) = 0;
	virtual void AdminChangeAccountAccess( const std::string& nick, const std::string& accesscode ) = 0;
	virtual void AdminSetBotMode( const std::string& nick, bool isbot ) = 0;

	virtual void HostBattle( BattleOptions bo ) = 0;
	virtual void JoinBattle( const Battle* battle, const std::string& password = "" ) = 0;
	virtual void LeaveBattle( const Battle* battle ) = 0;
	virtual void StartHostedBattle() = 0;

	virtual void ForceSide( const Battle* battle, const User* user, int side ) = 0;
	virtual void ForceTeam( const Battle* battle, const User* user, int team ) = 0;
	virtual void ForceAlly( const Battle* battle, const User* user, int ally ) = 0;
	virtual void ForceColour( const Battle* battle, const User* user, int r, int g, int b ) = 0;
	virtual void ForceSpectator( const Battle* battle, const User* user, bool spectator ) = 0;
	virtual void BattleKickPlayer( const Battle* battle, const User* user ) = 0;
	virtual void SetHandicap( const Battle* battle, const User* user, int handicap) = 0;

	virtual void AddBot( const Battle* battle, const std::string& nick, UserBattleStatus& status ) = 0;
	virtual void RemoveBot( const Battle* battle, const User* user ) = 0;
	virtual void UpdateBot( const Battle* battle, const User* user, UserBattleStatus& status ) = 0;

	virtual void SendHostInfo( HostInfo update ) = 0;
	virtual void SendHostInfo( const std::string& Tag ) = 0;
	virtual void SendRaw( const std::string& raw ) = 0;
	virtual void SendUserPosition( const User& usr ) = 0;

	virtual void RequestInGameTime( const std::string& nick ) = 0;

	virtual Battle* GetCurrentBattle() = 0;

	virtual void RequestChannels() = 0;

	virtual void SendMyBattleStatus( UserBattleStatus& bs ) = 0;
	virtual void SendMyUserStatus() = 0;

	virtual void SetKeepaliveInterval( int seconds ) { m_keepalive = seconds; }
	virtual int GetKeepaliveInterval() { return m_keepalive; }

	virtual bool IsPasswordHash( const std::string& pass ) const = 0;
	virtual std::string GetPasswordHash( const std::string& pass ) const = 0;

	std::string GetRequiredSpring() const { return m_min_required_spring_ver; }
	void SetRequiredSpring( const std::string& version ) { m_min_required_spring_ver = version; }

	virtual void OnConnected( Socket* sock ) = 0;
	virtual void OnDisconnected( Socket* sock ) = 0;
	virtual void OnDataReceived( Socket* sock ) = 0;

	virtual const User* GetMe() const {return m_me;}

	virtual void SendScriptToClients( const std::string& script ) = 0;

	std::map<std::string,std::string> m_channel_pw;  /// channel name -> password, filled on channel join

	std::string GetServerName() const { return m_server_name; }

	virtual void RequestSpringUpdate();

	virtual void SetRelayIngamePassword( const const User* user ) = 0;
	virtual StringVector GetRelayHostList();
	User* AcquireRelayhost();
	virtual void SendScriptToProxy( const std::string& script ) = 0;

	void SetPrivateUdpPort(int port) {m_udp_private_port = port;}
	std::string GenerateScriptPassword();

  protected:
	//! @brief map used internally by the iServer class to calculate ping roundtimes.
	typedef std::map<int, long long> PingList;

	Socket* m_sock;
	int m_keepalive; //! in seconds
	int m_ping_timeout; //! in seconds
	User* m_me;
	std::string m_server_name;
	std::string m_min_required_spring_ver;
	std::string m_last_denied_connection_reason;
	PingThread m_ping_thread;
    bool m_connected;
    bool m_online;
    unsigned long m_udp_private_port;
    unsigned long m_nat_helper_port;

	MutexWrapper<unsigned int> m_last_ping_id;
	unsigned int GetLastPingID()
	{
		ScopedLocker<unsigned int> l_last_ping_id(m_last_ping_id);
		return l_last_id.Get();
	}
	MutexWrapper<PingList> m_pinglist;
	PingList& GetPingList()
	{
		ScopedLocker<PingList> l_pinglist(m_pinglist);
		return l_pinglist.Get();
	}

	IServerEvents m_se;

	Battle* m_current_battle;

	User* m_relay_host_bot;
	User* m_relay_host_manager;

	StringVector m_relay_host_manager_list;

	User* _AddUser( const std::string& user, const int id );
	void _RemoveUser( const std::string& nickname );
	void _RemoveUser( const int id );

	Channel* _AddChannel( const std::string& chan );
	void _RemoveChannel( const std::string& name );

	Battle* _AddBattle( const int& id );
	void _RemoveBattle( const int& id );

	virtual void SendCmd( const std::string& command, const std::string& param ) = 0;
	virtual void RelayCmd( const std::string& command, const std::string& param ) = 0;
};