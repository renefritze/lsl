/* Copyright (C) 2007 The SpringLobby Team. All rights reserved. */

#if WIN32
    //uses __stdcall for bind
    #define BOOST_BIND_ENABLE_STDCALL
#endif

#include "c_api.h"

#include <stdexcept>
#include <cmath>
#include <boost/extension/shared_library.hpp>
#include <boost/foreach.hpp>

#include <utils/logging.h>
#include <utils/misc.h>
#include <utils/debug.h>
#include <utils/conversion.h>


#define UNITSYNC_EXCEPTION(cond,msg) do { if(!(cond))\
	LSL_THROW(unitsync,msg); } while(0)

#define LOCK_UNITSYNC boost::mutex::scoped_lock lock_criticalsection(m_lock)

//! Macro that checks if a function is present/loaded, unitsync is loaded, and locks it on call.
#define InitLib( arg ) \
	LOCK_UNITSYNC; \
	UNITSYNC_EXCEPTION( m_loaded, "Unitsync not loaded."); \
	UNITSYNC_EXCEPTION( arg, "Function was not in unitsync library.");

#define CHECK_FUNCTION( arg ) \
	do { if ( !(arg) ) LSL_THROW( function_missing, "arg" ); } while (0)

namespace LSL {

SpringUnitSyncLib::SpringUnitSyncLib()
	: m_loaded(false),
	m_libhandle(NULL),
	m_path(std::string()),
	m_init(NULL),
	m_uninit(NULL)
{
}


SpringUnitSyncLib::~SpringUnitSyncLib()
{
  Unload();
}

void SpringUnitSyncLib::Load( const std::string& path, const std::string& forceConfigFilePath )
{
	LOCK_UNITSYNC;

#ifdef WIN32
	//Dirty Hack to make the first character upper char
	//unitsync failed to initialize for me given a path like
	//"d:\Games\Spring\unitsync.dll"
	//but worked for "D:\Games\Spring\unitsync.dll"
	std::string g = path;
	if ( g.find( wxT( ":\\" ) ) == 1 )
	{
		g.SetChar( 0, wxToupper( g.at(0) ) );
	}
	_Load( g );
#else
	_Load( path );
#endif

	if ( !forceConfigFilePath.empty() )
	{
		if ( m_set_spring_config_file_path )
		{
			m_set_spring_config_file_path( forceConfigFilePath.c_str() );
		}
	}
	_Init();
}

//!here's some magic that helps us avoid lots of boilerplate for getting pointers
template < class FunctionPointerType, int argN >
struct LibFunc{};//if this gets instantiated you need to add more specializations :P

template < class F> struct LibFunc<F,0> {
	static F get( const std::string& name, boost::extensions::shared_library* lib )
	{ return lib->get<typename F::result_type>(name); }
};
template < class F> struct LibFunc<F,1> {
	static F get( const std::string& name, boost::extensions::shared_library* lib )
	{ return lib->get<typename F::result_type,typename F::arg1_type>(name); }
};
template < class F> struct LibFunc<F,2> {
	static F get( const std::string& name, boost::extensions::shared_library* lib )
	{ return lib->get<typename F::result_type,typename F::arg1_type,
				typename F::arg2_type>(name); }
};
template < class F> struct LibFunc<F,3> {
	static F get( const std::string& name, boost::extensions::shared_library* lib )
	{ return lib->get<typename F::result_type,typename F::arg1_type,
				typename F::arg2_type, typename F::arg3_type>(name); }
};
template < class F> struct LibFunc<F,4> {
	static F get( const std::string& name, boost::extensions::shared_library* lib )
	{ return lib->get<typename F::result_type,typename F::arg1_type,
				typename F::arg2_type, typename F::arg3_type,
				typename F::arg4_type>(name); }
};
template < class F> struct LibFunc<F,5> {
	static F get( const std::string& name, boost::extensions::shared_library* lib )
	{ return lib->get<typename F::result_type,typename F::arg1_type,
				typename F::arg2_type, typename F::arg3_type,
				typename F::arg4_type,typename F::arg5_type>(name); }
};

template < class FunctionPointerType >
void GetLibFuncPtr( boost::extensions::shared_library* libhandle, const std::string& name, FunctionPointerType& p )
{
	if ( !libhandle && libhandle->is_open() )
		throw std::runtime_error("libhandle not open");
	p = LibFunc<FunctionPointerType,FunctionPointerType::arity>::get( name, libhandle );
	if ( !p ) {
		LslError( "Couldn't load %s from unitsync library",name.c_str() );
	}
}

void SpringUnitSyncLib::_Load( const std::string& path )
{
	if ( _IsLoaded() && path == m_path ) return;

	_Unload();

	m_path = path;

	// Load the library.
	LslDebug( "Loading from: %s", path.c_str() );

	// Check if library exists
	if ( !Util::FileExists( path ) )
	{
		LslDebug( "File not found: %s", path.c_str() );
		LSL_THROW( file_not_found, path );
	}
	// Check if library readable
	if ( !Util::FileCanOpen( path ) )
	{
		LslDebug( "couldn read unitsync from %s", path.c_str() );
		LSL_THROW( unitsync, "lib at given location is present, but not readable." );
	}

	{
		try {
#ifdef __WXMSW__
			boost::filesystem::path us_path( path );
			boost::filesystem::current_path( path.parent_path() );
#endif
			m_libhandle = new boost::extensions::shared_library( path );
			if ( !m_libhandle->is_open() ) {
				delete m_libhandle;
				m_libhandle = 0;
			}
		} catch(...) {
			m_libhandle = 0;
		}
	}

	if (!m_libhandle)
		LSL_THROW( unitsync, "Couldn't load the unitsync library" );

	// Load all function from library.
	try {
		GetLibFuncPtr( m_libhandle, "Init",								m_init );
		GetLibFuncPtr( m_libhandle, "UnInit",							m_uninit );
		GetLibFuncPtr( m_libhandle, "GetNextError",						m_get_next_error );
		GetLibFuncPtr( m_libhandle, "GetWritableDataDirectory",			m_get_writeable_data_dir );
		GetLibFuncPtr( m_libhandle, "GetDataDirectory",					m_get_data_dir_by_index );
		GetLibFuncPtr( m_libhandle, "GetDataDirectoryCount",			m_get_data_dir_count );

		GetLibFuncPtr( m_libhandle, "GetMapCount",						m_get_map_count );
		GetLibFuncPtr( m_libhandle, "GetMapChecksum",					m_get_map_checksum );
		GetLibFuncPtr( m_libhandle, "GetMapName",						m_get_map_name );

		try {
			GetLibFuncPtr( m_libhandle, "GetMapDescription",			m_get_map_description );
			GetLibFuncPtr( m_libhandle, "GetMapAuthor",					m_get_map_author );
			GetLibFuncPtr( m_libhandle, "GetMapWidth",					m_get_map_width );
			GetLibFuncPtr( m_libhandle, "GetMapHeight",					m_get_map_height );
			GetLibFuncPtr( m_libhandle, "GetMapTidalStrength",			m_get_map_tidalStrength );
			GetLibFuncPtr( m_libhandle, "GetMapWindMin",				m_get_map_windMin );
			GetLibFuncPtr( m_libhandle, "GetMapWindMax",				m_get_map_windMax );
			GetLibFuncPtr( m_libhandle, "GetMapGravity",				m_get_map_gravity );
			GetLibFuncPtr( m_libhandle, "GetMapResourceCount",			m_get_map_resource_count );
			GetLibFuncPtr( m_libhandle, "GetMapResourceName",			m_get_map_resource_name );
			GetLibFuncPtr( m_libhandle, "GetMapResourceMax",			m_get_map_resource_max );
			GetLibFuncPtr( m_libhandle, "GetMapResourceExtractorRadius",m_get_map_resource_extractorRadius );
			GetLibFuncPtr( m_libhandle, "GetMapPosCount",				m_get_map_pos_count );
			GetLibFuncPtr( m_libhandle, "GetMapPosX",					m_get_map_pos_x );
			GetLibFuncPtr( m_libhandle, "GetMapPosZ",					m_get_map_pos_z );
			LslDebug("Using new style map-info fetching (GetMap*() functions).");
		}
		catch ( ... )
		{
			m_get_map_name = NULL;
			LslDebug("Using old style map-info fetching (GetMapInfoEx()).");
		}

		GetLibFuncPtr( m_libhandle, "GetMapInfoEx",						m_get_map_info_ex );
		GetLibFuncPtr( m_libhandle, "GetMinimap",						m_get_minimap );
		GetLibFuncPtr( m_libhandle, "GetInfoMapSize",					m_get_infomap_size );
		GetLibFuncPtr( m_libhandle, "GetInfoMap",						m_get_infomap );

		GetLibFuncPtr( m_libhandle, "GetPrimaryModChecksum",			m_get_mod_checksum );
		GetLibFuncPtr( m_libhandle, "GetPrimaryModIndex",				m_get_mod_index );
		GetLibFuncPtr( m_libhandle, "GetPrimaryModName",				m_get_mod_name );
		GetLibFuncPtr( m_libhandle, "GetPrimaryModCount",				m_get_mod_count );
		GetLibFuncPtr( m_libhandle, "GetPrimaryModArchive",				m_get_mod_archive );

		GetLibFuncPtr( m_libhandle, "GetSideCount",						m_get_side_count );
		GetLibFuncPtr( m_libhandle, "GetSideName",						m_get_side_name );

		GetLibFuncPtr( m_libhandle, "AddAllArchives",					m_add_all_archives );
		GetLibFuncPtr( m_libhandle, "RemoveAllArchives",				m_remove_all_archives );

		GetLibFuncPtr( m_libhandle, "GetUnitCount",						m_get_unit_count );
		GetLibFuncPtr( m_libhandle, "GetUnitName",						m_get_unit_name );
		GetLibFuncPtr( m_libhandle, "GetFullUnitName",					m_get_unit_full_name );
		GetLibFuncPtr( m_libhandle, "ProcessUnitsNoChecksum",			m_proc_units_nocheck );

		GetLibFuncPtr( m_libhandle, "InitFindVFS",						m_init_find_vfs );
		GetLibFuncPtr( m_libhandle, "FindFilesVFS",						m_find_files_vfs );
		GetLibFuncPtr( m_libhandle, "OpenFileVFS",						m_open_file_vfs );
		GetLibFuncPtr( m_libhandle, "FileSizeVFS",						m_file_size_vfs );
		GetLibFuncPtr( m_libhandle, "ReadFileVFS",						m_read_file_vfs );
		GetLibFuncPtr( m_libhandle, "CloseFileVFS",						m_close_file_vfs );

		GetLibFuncPtr( m_libhandle, "GetSpringVersion",					m_get_spring_version );

		GetLibFuncPtr( m_libhandle, "ProcessUnits",						m_process_units );
		GetLibFuncPtr( m_libhandle, "AddArchive",						m_add_archive );
		GetLibFuncPtr( m_libhandle, "GetArchiveChecksum",				m_get_archive_checksum );
		GetLibFuncPtr( m_libhandle, "GetArchivePath",					m_get_archive_path );

		GetLibFuncPtr( m_libhandle, "GetMapArchiveCount",				m_get_map_archive_count );
		GetLibFuncPtr( m_libhandle, "GetMapArchiveName",				m_get_map_archive_name );
		GetLibFuncPtr( m_libhandle, "GetMapChecksum",					m_get_map_checksum );
		GetLibFuncPtr( m_libhandle, "GetMapChecksumFromName",			m_get_map_checksum_from_name );

		GetLibFuncPtr( m_libhandle, "GetPrimaryModShortName",			m_get_primary_mod_short_name );
		GetLibFuncPtr( m_libhandle, "GetPrimaryModVersion",				m_get_primary_mod_version );
		GetLibFuncPtr( m_libhandle, "GetPrimaryModMutator",				m_get_primary_mod_mutator );
		GetLibFuncPtr( m_libhandle, "GetPrimaryModGame",				m_get_primary_mod_game );
		GetLibFuncPtr( m_libhandle, "GetPrimaryModShortGame",			m_get_primary_mod_short_game );
		GetLibFuncPtr( m_libhandle, "GetPrimaryModDescription",			m_get_primary_mod_description );
		GetLibFuncPtr( m_libhandle, "GetPrimaryModArchive",				m_get_primary_mod_archive );
		GetLibFuncPtr( m_libhandle, "GetPrimaryModArchiveCount",		m_get_primary_mod_archive_count );
		GetLibFuncPtr( m_libhandle, "GetPrimaryModArchiveList",			m_get_primary_mod_archive_list );
		GetLibFuncPtr( m_libhandle, "GetPrimaryModChecksumFromName",	m_get_primary_mod_checksum_from_name );

		GetLibFuncPtr( m_libhandle, "GetModValidMapCount",				m_get_mod_valid_map_count );
		GetLibFuncPtr( m_libhandle, "GetModValidMap",					m_get_valid_map );

		GetLibFuncPtr( m_libhandle, "GetLuaAICount",					m_get_luaai_count );
		GetLibFuncPtr( m_libhandle, "GetLuaAIName",						m_get_luaai_name );
		GetLibFuncPtr( m_libhandle, "GetLuaAIDesc",						m_get_luaai_desc );

		GetLibFuncPtr( m_libhandle, "GetMapOptionCount",				m_get_map_option_count );
		GetLibFuncPtr( m_libhandle, "GetCustomOptionCount",				m_get_custom_option_count );
		GetLibFuncPtr( m_libhandle, "GetModOptionCount",				m_get_mod_option_count );
		GetLibFuncPtr( m_libhandle, "GetSkirmishAIOptionCount",			m_get_skirmish_ai_option_count );
		GetLibFuncPtr( m_libhandle, "GetOptionKey",						m_get_option_key );
		GetLibFuncPtr( m_libhandle, "GetOptionName",					m_get_option_name );
		GetLibFuncPtr( m_libhandle, "GetOptionDesc",					m_get_option_desc );
		GetLibFuncPtr( m_libhandle, "GetOptionType",					m_get_option_type );
		GetLibFuncPtr( m_libhandle, "GetOptionSection",					m_get_option_section );
		GetLibFuncPtr( m_libhandle, "GetOptionStyle",					m_get_option_style );
		GetLibFuncPtr( m_libhandle, "GetOptionBoolDef",					m_get_option_bool_def );
		GetLibFuncPtr( m_libhandle, "GetOptionNumberDef",				m_get_option_number_def );
		GetLibFuncPtr( m_libhandle, "GetOptionNumberMin",				m_get_option_number_min );
		GetLibFuncPtr( m_libhandle, "GetOptionNumberMax",				m_get_option_number_max );
		GetLibFuncPtr( m_libhandle, "GetOptionNumberStep",				m_get_option_number_step );
		GetLibFuncPtr( m_libhandle, "GetOptionStringDef",				m_get_option_string_def );
		GetLibFuncPtr( m_libhandle, "GetOptionStringMaxLen",			m_get_option_string_max_len );
		GetLibFuncPtr( m_libhandle, "GetOptionListCount",				m_get_option_list_count );
		GetLibFuncPtr( m_libhandle, "GetOptionListDef",					m_get_option_list_def );
		GetLibFuncPtr( m_libhandle, "GetOptionListItemKey",				m_get_option_list_item_key );
		GetLibFuncPtr( m_libhandle, "GetOptionListItemName",			m_get_option_list_item_name );
		GetLibFuncPtr( m_libhandle, "GetOptionListItemDesc",			m_get_option_list_item_desc );

		GetLibFuncPtr( m_libhandle, "SetSpringConfigFile",				m_set_spring_config_file_path );
		GetLibFuncPtr( m_libhandle, "GetSpringConfigFile",				m_get_spring_config_file_path );

		GetLibFuncPtr( m_libhandle, "OpenArchive",						m_open_archive );
		GetLibFuncPtr( m_libhandle, "CloseArchive",						m_close_archive );
		GetLibFuncPtr( m_libhandle, "FindFilesArchive",					m_find_Files_archive );
		GetLibFuncPtr( m_libhandle, "OpenArchiveFile",					m_open_archive_file );
		GetLibFuncPtr( m_libhandle, "ReadArchiveFile",					m_read_archive_file );
		GetLibFuncPtr( m_libhandle, "CloseArchiveFile",					m_close_archive_file );
		GetLibFuncPtr( m_libhandle, "SizeArchiveFile",					m_size_archive_file );

		GetLibFuncPtr( m_libhandle, "SetSpringConfigFloat",				m_set_spring_config_float );
		GetLibFuncPtr( m_libhandle, "GetSpringConfigFloat",				m_get_spring_config_float );
		GetLibFuncPtr( m_libhandle, "GetSpringConfigInt",				m_get_spring_config_int );
		GetLibFuncPtr( m_libhandle, "GetSpringConfigString",			m_get_spring_config_string );
		GetLibFuncPtr( m_libhandle, "SetSpringConfigString",			m_set_spring_config_string );
		GetLibFuncPtr( m_libhandle, "SetSpringConfigInt",				m_set_spring_config_int );

		GetLibFuncPtr( m_libhandle, "GetSkirmishAICount",				m_get_skirmish_ai_count );
		GetLibFuncPtr( m_libhandle, "GetSkirmishAIInfoCount",			m_get_skirmish_ai_info_count );
		GetLibFuncPtr( m_libhandle, "GetInfoKey",						m_get_skirmish_ai_info_key );
		GetLibFuncPtr( m_libhandle, "GetInfoValue",						m_get_skirmish_ai_info_value );
		GetLibFuncPtr( m_libhandle, "GetInfoDescription",				m_get_skirmish_ai_info_description );

		// begin lua parser calls

		GetLibFuncPtr( m_libhandle, "lpClose",							m_parser_close );
		GetLibFuncPtr( m_libhandle, "lpOpenFile",						m_parser_open_file );
		GetLibFuncPtr( m_libhandle, "lpOpenSource",						m_parser_open_source );
		GetLibFuncPtr( m_libhandle, "lpExecute",						m_parser_execute );
		GetLibFuncPtr( m_libhandle, "lpErrorLog",						m_parser_error_log );

		GetLibFuncPtr( m_libhandle, "lpAddTableInt",					m_parser_add_table_int );
		GetLibFuncPtr( m_libhandle, "lpAddTableStr",					m_parser_add_table_string );
		GetLibFuncPtr( m_libhandle, "lpEndTable",						m_parser_end_table );
		GetLibFuncPtr( m_libhandle, "lpAddIntKeyIntVal",				m_parser_add_int_key_int_value );
		GetLibFuncPtr( m_libhandle, "lpAddStrKeyIntVal",				m_parser_add_string_key_int_value );
		GetLibFuncPtr( m_libhandle, "lpAddIntKeyBoolVal",				m_parser_add_int_key_bool_value );
		GetLibFuncPtr( m_libhandle, "lpAddStrKeyBoolVal",				m_parser_add_string_key_bool_value );
		GetLibFuncPtr( m_libhandle, "lpAddIntKeyFloatVal",				m_parser_add_int_key_float_value );
		GetLibFuncPtr( m_libhandle, "lpAddStrKeyFloatVal",				m_parser_add_string_key_float_value );
		GetLibFuncPtr( m_libhandle, "lpAddIntKeyStrVal",				m_parser_add_int_key_string_value );
		GetLibFuncPtr( m_libhandle, "lpAddStrKeyStrVal",				m_parser_add_string_key_string_value );

		GetLibFuncPtr( m_libhandle, "lpRootTable",						m_parser_root_table );
		GetLibFuncPtr( m_libhandle, "lpRootTableExpr",					m_parser_root_table_expression );
		GetLibFuncPtr( m_libhandle, "lpSubTableInt",					m_parser_sub_table_int );
		GetLibFuncPtr( m_libhandle, "lpSubTableStr",					m_parser_sub_table_string );
		GetLibFuncPtr( m_libhandle, "lpSubTableExpr",					m_parser_sub_table_expression );
		GetLibFuncPtr( m_libhandle, "lpPopTable",						m_parser_pop_table );

		GetLibFuncPtr( m_libhandle, "lpGetKeyExistsInt",				m_parser_key_int_exists );
		GetLibFuncPtr( m_libhandle, "lpGetKeyExistsStr",				m_parser_key_string_exists );

		GetLibFuncPtr( m_libhandle, "lpGetIntKeyType",					m_parser_int_key_get_type );
		GetLibFuncPtr( m_libhandle, "lpGetStrKeyType",					m_parser_string_key_get_type );

		GetLibFuncPtr( m_libhandle, "lpGetIntKeyListCount",				m_parser_int_key_get_list_count );
		GetLibFuncPtr( m_libhandle, "lpGetIntKeyListEntry",				m_parser_int_key_get_list_entry );
		GetLibFuncPtr( m_libhandle, "lpGetStrKeyListCount",				m_parser_string_key_get_list_count );
		GetLibFuncPtr( m_libhandle, "lpGetStrKeyListEntry",				m_parser_string_key_get_list_entry );

		GetLibFuncPtr( m_libhandle, "lpGetIntKeyIntVal",				m_parser_int_key_get_int_value );
		GetLibFuncPtr( m_libhandle, "lpGetStrKeyIntVal",				m_parser_string_key_get_int_value );
		GetLibFuncPtr( m_libhandle, "lpGetIntKeyBoolVal",				m_parser_int_key_get_bool_value );
		GetLibFuncPtr( m_libhandle, "lpGetStrKeyBoolVal",				m_parser_string_key_get_bool_value );
		GetLibFuncPtr( m_libhandle, "lpGetIntKeyFloatVal",				m_parser_int_key_get_float_value );
		GetLibFuncPtr( m_libhandle, "lpGetStrKeyFloatVal",				m_parser_string_key_get_float_value );
		GetLibFuncPtr( m_libhandle, "lpGetIntKeyStrVal",				m_parser_int_key_get_string_value );
		GetLibFuncPtr( m_libhandle, "lpGetStrKeyStrVal",				m_parser_string_key_get_string_value );

		// only when we end up here unitsync was succesfully loaded.
		m_loaded = true;
	}
	catch ( ... )
	{
		// don't uninit unitsync in _Unload -- it hasn't been init'ed yet
		m_uninit = NULL;
		_Unload();
		LSL_THROW( unitsync, "Failed to load Unitsync lib.");
	}
}


void SpringUnitSyncLib::_Init()
{
	if ( _IsLoaded() && m_init != NULL )
	{
		m_current_mod = std::string();
		m_init( true, 1 );

		BOOST_FOREACH( const std::string error, GetUnitsyncErrors() )
		{
			LslError( "%s", error.c_str() );
		}
	}
}


void SpringUnitSyncLib::_RemoveAllArchives()
{
	if (m_remove_all_archives)
		m_remove_all_archives();
	else
		_Init();
}


void SpringUnitSyncLib::Unload()
{
	if ( !_IsLoaded() ) return;// dont even lock anything if unloaded.
	LOCK_UNITSYNC;

	_Unload();
}


void SpringUnitSyncLib::_Unload()
{
	// as soon as we enter m_uninit unitsync technically isn't loaded anymore.
	m_loaded = false;

	m_path = std::string();

	// can't call UnSetCurrentMod() because it takes the unitsync lock
	m_current_mod = std::string();

	if (m_uninit)
		m_uninit();

	delete m_libhandle;
	m_libhandle = NULL;

	m_init = NULL;
	m_uninit = NULL;
}


bool SpringUnitSyncLib::IsLoaded() const
{
	return m_loaded;
}


bool SpringUnitSyncLib::_IsLoaded() const
{
	return m_loaded;
}


void SpringUnitSyncLib::AssertUnitsyncOk() const
{
	UNITSYNC_EXCEPTION( m_loaded, "Unitsync not loaded.");
	UNITSYNC_EXCEPTION( m_get_next_error, "Function was not in unitsync library.");
	UNITSYNC_EXCEPTION( false, m_get_next_error() );
}


std::vector<std::string> SpringUnitSyncLib::GetUnitsyncErrors() const
{
	std::vector<std::string> ret;
	try
	{
		UNITSYNC_EXCEPTION( m_loaded, "Unitsync not loaded.");
		UNITSYNC_EXCEPTION( m_get_next_error, "Function was not in unitsync library.");

		std::string msg = m_get_next_error();
		while ( !msg.empty() )
		{
			ret.push_back( msg );
			msg = m_get_next_error();
		}
		return ret;
	}
	catch ( unitsync_assert &e )
	{
		ret.push_back( e.what() );
		return ret;
	}
}


bool SpringUnitSyncLib::VersionSupports( LSL::GameFeature feature ) const
{
	LOCK_UNITSYNC;

	switch (feature)
	{
		case LSL::USYNC_Sett_Handler: return m_set_spring_config_string;
		case LSL::USYNC_GetInfoMap:   return m_get_infomap_size;
		case LSL::USYNC_GetDataDir:   return m_get_writeable_data_dir;
		case LSL::USYNC_GetSkirmishAI:   return m_get_skirmish_ai_count;
		default: return false;
	}
}

void SpringUnitSyncLib::_ConvertSpringMapInfo( const SpringMapInfo& in, MapInfo& out )
{
	out.author = in.author;
	out.description = in.description;
	out.extractorRadius = in.extractorRadius;
	out.gravity = in.gravity;
	out.tidalStrength = in.tidalStrength;
	out.maxMetal = in.maxMetal;
	out.minWind = in.minWind;
	out.maxWind = in.maxWind;
	out.width = in.width;
	out.height = in.height;
	out.positions = std::vector<StartPos>( in.positions, in.positions + in.posCount );
}


void SpringUnitSyncLib::SetCurrentMod( const std::string& modname )
{
	InitLib( m_init ); // assumes the others are fine
	// (m_add_all_archives, m_get_mod_archive, m_get_mod_index)

	_SetCurrentMod( modname );
}


void SpringUnitSyncLib::_SetCurrentMod( const std::string& modname )
{
	if ( m_current_mod != modname )
	{
		if ( !m_current_mod.empty() ) _RemoveAllArchives();
		m_add_all_archives( m_get_mod_archive( m_get_mod_index( modname.c_str() ) ) );
		m_current_mod = modname;
	}
}


void SpringUnitSyncLib::UnSetCurrentMod( )
{
	LOCK_UNITSYNC;
	if ( !m_current_mod.empty() ) _RemoveAllArchives();
	m_current_mod = std::string();
}


int SpringUnitSyncLib::GetModIndex( const std::string& name )
{
	return GetPrimaryModIndex( name );
}


std::map<std::string, std::string> SpringUnitSyncLib::GetSpringVersionList(const std::map<std::string, std::string>& usync_paths)
{
	LOCK_UNITSYNC;
	std::map<std::string, std::string> ret;

	for (std::map<std::string, std::string>::const_iterator it = usync_paths.begin(); it != usync_paths.end(); ++it)
	{
		std::string path = it->second;
		try
		{

			if ( !Util::FileExists( path ) )
			{
				LSL_THROW( file_not_found, path);
			}

#ifdef __WXMSW__
			boost::filesystem::path us_path( path );
			boost::filesystem::current_path( path.parent_path() );
#endif
			boost::extensions::shared_library temphandle( path );
			if( !temphandle.is_open())
				LSL_THROW(unitsync, "Couldn't load the unitsync library");

			GetSpringVersionPtr getspringversion = 0;
			std::string functionname = "GetSpringVersion";
			GetLibFuncPtr( &temphandle, functionname, getspringversion );
			if( !getspringversion )
				LSL_THROW( unitsync, "getspringversion: function not found");
			std::string version = getspringversion();
			LslDebug( "Found spring version: %s", version.c_str() );
			ret[it->first] = version;
		}
		catch(...){}
	}
	return ret;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//  -- The UnitSync functions --
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////


std::string SpringUnitSyncLib::GetSpringVersion()
{
	InitLib( m_get_spring_version );

	return m_get_spring_version();
}

std::string SpringUnitSyncLib::GetSpringDataDir()
{
	InitLib( m_get_writeable_data_dir );

	return m_get_writeable_data_dir();
}

int SpringUnitSyncLib::GetSpringDataDirCount()
{
	InitLib( m_get_data_dir_count);

	return m_get_data_dir_count();
}

std::string SpringUnitSyncLib::GetSpringDataDirByIndex( const int index )
{
	InitLib( m_get_data_dir_by_index );

	return m_get_data_dir_by_index( index );
}

std::string SpringUnitSyncLib::GetConfigFilePath()
{
	InitLib( m_get_spring_config_file_path );

	return m_get_spring_config_file_path();
}


int SpringUnitSyncLib::GetMapCount()
{
	InitLib( m_get_map_count );

	return m_get_map_count();
}


std::string SpringUnitSyncLib::GetMapChecksum( int index )
{
	InitLib( m_get_map_checksum );

	return Util::ToString( (unsigned int)m_get_map_checksum( index ) );
}


std::string SpringUnitSyncLib::GetMapName( int index )
{
	InitLib( m_get_map_name );

	return m_get_map_name( index );
}


int SpringUnitSyncLib::GetMapArchiveCount( int index )
{
	InitLib( m_get_map_archive_count );

	return m_get_map_archive_count( m_get_map_name( index ) );
}


std::string SpringUnitSyncLib::GetMapArchiveName( int arnr )
{
	InitLib( m_get_map_archive_name );

	return m_get_map_archive_name( arnr );
}


SpringUnitSyncLib::StringVector SpringUnitSyncLib::GetMapDeps( int index )
{
	int count = GetMapArchiveCount( index );
	StringVector ret;
	for ( int i = 0; i < count; i++ )
	{
		ret.push_back( GetMapArchiveName( i ) );
	}
	return ret;
}


MapInfo SpringUnitSyncLib::GetMapInfoEx( int index, int version )
{
	if (m_get_map_description == NULL) {
		// old fetch method
		InitLib( m_get_map_info_ex );

		const std::string& mapName =  m_get_map_name( index );

		char tmpdesc[256];
		char tmpauth[256];

		MapInfo info;

		SpringMapInfo tm;
		tm.description = &tmpdesc[0];
		tm.author = &tmpauth[0];

		bool result = m_get_map_info_ex( mapName.c_str(), &tm, version );
		if (!result)
			LSL_THROW( unitsync, "Failed to get map infos");
		_ConvertSpringMapInfo( tm, info );
		return info;
	} else {
		// new fetch method
		InitLib( m_get_map_description )

				MapInfo info;

		info.description = m_get_map_description( index);
		info.tidalStrength = m_get_map_tidalStrength(index);
		info.gravity = m_get_map_gravity(index);

		const int resCount = m_get_map_resource_count(index);
		if (resCount > 0) {
			const int resourceIndex = 0;
			info.maxMetal = m_get_map_resource_max(index, resourceIndex);
			info.extractorRadius = m_get_map_resource_extractorRadius(index, resourceIndex);
		} else {
			info.maxMetal = 0.0f;
			info.extractorRadius = 0.0f;
		}

		info.minWind = m_get_map_windMin(index);
		info.maxWind = m_get_map_windMax(index);

		info.width = m_get_map_width(index);
		info.height = m_get_map_height(index);
		const int posCount = m_get_map_pos_count(index);
		for (int p = 0; p < posCount; ++p) {
			StartPos sp;
			sp.x = m_get_map_pos_x(index, p);
			sp.y = m_get_map_pos_z(index, p);
			info.positions.push_back(sp);
		}
		info.author = m_get_map_author(index);
		return info;
	}
}


UnitSyncImage SpringUnitSyncLib::GetMinimap( const std::string& mapFileName )
{
	InitLib( m_get_minimap );

	const int miplevel = 0;  // miplevel should not be 10 ffs
	const int width  = 1024 >> miplevel;
	const int height = 1024 >> miplevel;

	LslDebug( "Minimap: %s", mapFileName.c_str() );

	// this unitsync call returns a pointer to a static buffer
	unsigned short* colours = (unsigned short*)m_get_minimap( mapFileName.c_str(), miplevel );
	if (!colours)
		LSL_THROW( unitsync, "Get minimap failed");

	typedef unsigned char uchar;
	UnitSyncImage minimap(width, height, false);
	uchar* true_colours = minimap.GetData();

	for ( int i = 0; i < width*height; i++ ) {
		true_colours[(i*3)  ] = uchar( (( colours[i] >> 11 )/31.0)*255.0 );
		true_colours[(i*3)+1] = uchar( (( (colours[i] >> 5) & 63 )/63.0)*255.0 );
		true_colours[(i*3)+2] = uchar( (( colours[i] & 31 )/31.0)*255.0 );
	}

	return minimap;
}


UnitSyncImage SpringUnitSyncLib::GetMetalmap( const std::string& mapFileName )
{
	InitLib( m_get_infomap_size ); // assume GetInfoMap is available too

	LslDebug( "Metalmap: %s", mapFileName.c_str() );

	int width = 0, height = 0, retval;

	retval = m_get_infomap_size(mapFileName.c_str(), "metal", &width, &height);
	if ( !(retval != 0 && width * height != 0) )
		LSL_THROW( unitsync, "Get metalmap size failed");

	typedef unsigned char uchar;
	UnitSyncImage metalmap(width, height, false);
	uninitialized_array<uchar> grayscale(width * height);
	uchar* true_colours = metalmap.GetData();

	retval = m_get_infomap(mapFileName.c_str(), "metal", grayscale, 1 /*byte per pixel*/);
	if ( retval == 0 )
		LSL_THROW( unitsync, "Get metalmap failed");

	for ( int i = 0; i < width*height; i++ ) {
		true_colours[(i*3)  ] = 0;
		true_colours[(i*3)+1] = grayscale[i];
		true_colours[(i*3)+2] = 0;
	}

	return metalmap;
}


UnitSyncImage SpringUnitSyncLib::GetHeightmap( const std::string& mapFileName )
{
	InitLib( m_get_infomap_size ); // assume GetInfoMap is available too

	LslDebug( "Heightmap: %s", mapFileName.c_str() );

	int width = 0, height = 0, retval;

	retval = m_get_infomap_size(mapFileName.c_str(), "height", &width, &height);
	if ( !(retval != 0 && width * height != 0) )
		LSL_THROW( unitsync, "Get heightmap size failed");

	typedef unsigned char uchar;
	typedef unsigned short ushort;
	UnitSyncImage heightmap(width, height, false);
	uninitialized_array<ushort> grayscale(width * height);
	uchar* true_colours = heightmap.GetData();

	retval = m_get_infomap(mapFileName.c_str(), "height", grayscale, 2 /*byte per pixel*/);
	if ( retval == 0 )
		LSL_THROW( unitsync, "Get heightmap failed");

	// the height is mapped to this "palette" of colors
	// the colors are linearly interpolated

	const uchar points[][3] = {
		{   0,   0,   0 },
		{   0,   0, 255 },
		{   0, 255, 255 },
		{   0, 255,   0 },
		{ 255, 255,   0 },
		{ 255,   0,   0 },
	};
	const int numPoints = sizeof(points) / sizeof(points[0]);

	// find range of values present in the height data returned by unitsync
	int min = 65536;
	int max = 0;

	for ( int i = 0; i < width*height; i++ ) {
		if (grayscale[i] < min) min = grayscale[i];
		if (grayscale[i] > max) max = grayscale[i];
	}

	// prevent division by zero -- heightmap wouldn't contain any information anyway
	if (min == max)
		return UnitSyncImage( 1, 1 );

	// perform the mapping from 16 bit grayscale to 24 bit true colour
	const double range = max - min + 1;
	for ( int i = 0; i < width*height; i++ ) {
		const double value = (grayscale[i] - min) / (range / (numPoints - 1));
		const int idx1 = int(value);
		const int idx2 = idx1 + 1;
		const int t = int(256.0 * (value - std::floor(value)));

		//assert(idx1 >= 0 && idx1 < numPoints-1);
		//assert(idx2 >= 1 && idx2 < numPoints);
		//assert(t >= 0 && t <= 255);

		for ( int j = 0; j < 3; ++j)
			true_colours[(i*3)+j] = (points[idx1][j] * (255 - t) + points[idx2][j] * t) / 255;
	}

	return heightmap;
}


std::string SpringUnitSyncLib::GetPrimaryModChecksum( int index )
{
	InitLib( m_get_mod_checksum );
	return Util::ToString( (unsigned int)m_get_mod_checksum( index ) );
}


int SpringUnitSyncLib::GetPrimaryModIndex( const std::string& modName )
{
	InitLib( m_get_mod_index );

	return m_get_mod_index( modName.c_str() );
}


std::string SpringUnitSyncLib::GetPrimaryModName( int index )
{
	InitLib( m_get_mod_name );

	return m_get_mod_name( index );
}


int SpringUnitSyncLib::GetPrimaryModCount()
{
	InitLib( m_get_mod_count );

	return m_get_mod_count();
}


std::string SpringUnitSyncLib::GetPrimaryModArchive( int index )
{
	InitLib( m_get_mod_archive );
	if (!m_get_mod_count)
		LSL_THROW( unitsync, "Function was not in unitsync library.");

	int count = m_get_mod_count();
	if (index >= count)
		LSL_THROW( unitsync, "index out of bounds");
	return m_get_mod_archive( index );
}


std::string SpringUnitSyncLib::GetPrimaryModShortName( int index )
{
	InitLib( m_get_primary_mod_short_name );

	return m_get_primary_mod_short_name( index );
}


std::string SpringUnitSyncLib::GetPrimaryModVersion( int index )
{
	InitLib( m_get_primary_mod_version );

	return m_get_primary_mod_version( index );
}


std::string SpringUnitSyncLib::GetPrimaryModMutator( int index )
{
	InitLib( m_get_primary_mod_mutator );

	return m_get_primary_mod_mutator( index );
}


std::string SpringUnitSyncLib::GetPrimaryModGame( int index )
{
	InitLib( m_get_primary_mod_game );

	return m_get_primary_mod_game( index );
}


std::string SpringUnitSyncLib::GetPrimaryModShortGame( int index )
{
	InitLib( m_get_primary_mod_short_game );

	return m_get_primary_mod_short_game( index );
}


std::string SpringUnitSyncLib::GetPrimaryModDescription( int index )
{
	InitLib( m_get_primary_mod_description );

	return m_get_primary_mod_description( index );
}


int SpringUnitSyncLib::GetPrimaryModArchiveCount( int index )
{
	InitLib( m_get_primary_mod_archive_count );

	return m_get_primary_mod_archive_count( index );
}


std::string SpringUnitSyncLib::GetPrimaryModArchiveList( int arnr )
{
	InitLib( m_get_primary_mod_archive_list );

	return m_get_primary_mod_archive_list( arnr );
}


std::string SpringUnitSyncLib::GetPrimaryModChecksumFromName( const std::string& name )
{
	InitLib( m_get_primary_mod_checksum_from_name );

	return Util::ToString( (unsigned int)m_get_primary_mod_checksum_from_name( name.c_str() ) );
}


SpringUnitSyncLib::StringVector SpringUnitSyncLib::GetModDeps( int index )
{
	int count = GetPrimaryModArchiveCount( index );
	StringVector ret;
	for ( int i = 0; i < count; i++ )
	{
		ret.push_back( GetPrimaryModArchiveList( i ) );
	}
	return ret;
}


SpringUnitSyncLib::StringVector SpringUnitSyncLib::GetSides( const std::string& modName )
{
	InitLib( m_get_side_count );
	if (!m_get_side_name)
		LSL_THROW( function_missing, "m_get_side_name");

			_SetCurrentMod( modName );
	int count = m_get_side_count();
	StringVector ret;
	for ( int i = 0; i < count; i ++ )
		ret.push_back( m_get_side_name( i ) );
	return ret;
}


void SpringUnitSyncLib::AddAllArchives( const std::string& root )
{
	InitLib( m_add_all_archives );

	m_add_all_archives( root.c_str() );
}


std::string SpringUnitSyncLib::GetFullUnitName( int index )
{
	InitLib( m_get_unit_full_name );

	return m_get_unit_full_name( index );
}


std::string SpringUnitSyncLib::GetUnitName( int index )
{
	InitLib( m_get_unit_name );

	return m_get_unit_name( index );
}


int SpringUnitSyncLib::GetUnitCount()
{
	InitLib( m_get_unit_count );

	return m_get_unit_count();
}


int SpringUnitSyncLib::ProcessUnitsNoChecksum()
{
	InitLib( m_proc_units_nocheck );

	return m_proc_units_nocheck();
}


SpringUnitSyncLib::StringVector SpringUnitSyncLib::FindFilesVFS( const std::string& name )
{
	InitLib( m_find_files_vfs );
	CHECK_FUNCTION( m_init_find_vfs );
	int handle = m_init_find_vfs( name.c_str() );
	StringVector ret;
	//thanks to assbars awesome edit we now get different invalid values from init and find
	if ( handle != -1 ) {
		do
		{
			char buffer[1025];
			handle = m_find_files_vfs( handle, &buffer[0], 1024 );
			buffer[1024] = 0;
			ret.push_back( &buffer[0] );
		}while ( handle );
	}
	return ret;
}


int SpringUnitSyncLib::OpenFileVFS( const std::string& name )
{
	InitLib( m_open_file_vfs );

	return m_open_file_vfs( name.c_str() );
}


int SpringUnitSyncLib::FileSizeVFS( int handle )
{
	InitLib( m_file_size_vfs );

	return m_file_size_vfs( handle );
}


int SpringUnitSyncLib::ReadFileVFS( int handle, void* buffer, int bufferLength )
{
	InitLib( m_read_file_vfs );

	return m_read_file_vfs( handle, buffer, bufferLength );
}


void SpringUnitSyncLib::CloseFileVFS( int handle )
{
	InitLib( m_close_file_vfs );

	m_close_file_vfs( handle );
}


int SpringUnitSyncLib::GetLuaAICount( const std::string& modname )
{
	InitLib( m_get_luaai_count );

	_SetCurrentMod( modname );
	return m_get_luaai_count();
}


std::string SpringUnitSyncLib::GetLuaAIName( int aiIndex )
{
	InitLib( m_get_luaai_name );

	return m_get_luaai_name( aiIndex );
}


std::string SpringUnitSyncLib::GetLuaAIDesc( int aiIndex )
{
	InitLib( m_get_luaai_desc );

	return m_get_luaai_desc( aiIndex );
}

unsigned int SpringUnitSyncLib::GetValidMapCount( const std::string& modname )
{
	InitLib( m_get_mod_valid_map_count );

	_SetCurrentMod( modname );
	return m_get_mod_valid_map_count();
}


std::string SpringUnitSyncLib::GetValidMapName( unsigned int MapIndex )
{
	InitLib( m_get_valid_map );

	return m_get_valid_map( MapIndex );
}


int SpringUnitSyncLib::GetMapOptionCount( const std::string& name )
{
	InitLib( m_get_map_option_count );
	if (name.empty())
		LSL_THROW( unitsync, "tried to pass empty mapname to unitsync");

	return m_get_map_option_count( name.c_str() );
}

int SpringUnitSyncLib::GetCustomOptionCount( const std::string& archive_name, const std::string& filename )
{
	InitLib( m_get_custom_option_count );
	if (archive_name.empty())
		LSL_THROW( unitsync, "tried to pass empty archive_name to unitsync");
	_RemoveAllArchives();
	m_add_all_archives( archive_name.c_str() );
	return m_get_custom_option_count( filename.c_str() );
}


int SpringUnitSyncLib::GetModOptionCount( const std::string& name )
{
	InitLib( m_get_mod_option_count );
	if (name.empty())
		LSL_THROW( unitsync, "tried to pass empty modname to unitsync");

	_SetCurrentMod( name );
	return m_get_mod_option_count();
}


int SpringUnitSyncLib::GetAIOptionCount( const std::string& modname, int aiIndex )
{
	InitLib( m_get_skirmish_ai_option_count );
	_SetCurrentMod( modname );
	CHECK_FUNCTION( m_get_skirmish_ai_count );

	if ( !(( aiIndex >= 0 ) && ( aiIndex < m_get_skirmish_ai_count() )) )
		LSL_THROW( unitsync, "aiIndex out of bounds");

	return m_get_skirmish_ai_option_count( aiIndex );
}


std::string SpringUnitSyncLib::GetOptionKey( int optIndex )
{
	InitLib( m_get_option_key );

	return m_get_option_key( optIndex );
}


std::string SpringUnitSyncLib::GetOptionName( int optIndex )
{
	InitLib( m_get_option_name );

	return m_get_option_name( optIndex );
}


std::string SpringUnitSyncLib::GetOptionDesc( int optIndex )
{
	InitLib( m_get_option_desc );

	return m_get_option_desc( optIndex );
}

std::string SpringUnitSyncLib::GetOptionSection( int optIndex )
{
	InitLib( m_get_option_section );

	return m_get_option_section( optIndex );
}

std::string SpringUnitSyncLib::GetOptionStyle( int optIndex )
{
	InitLib( m_get_option_style );

	return m_get_option_style( optIndex );
}


int SpringUnitSyncLib::GetOptionType( int optIndex )
{
	InitLib( m_get_option_type );

	return m_get_option_type( optIndex );
}


int SpringUnitSyncLib::GetOptionBoolDef( int optIndex )
{
	InitLib( m_get_option_bool_def );

	return m_get_option_bool_def( optIndex );
}


float SpringUnitSyncLib::GetOptionNumberDef( int optIndex )
{
	InitLib( m_get_option_number_def );

	return m_get_option_number_def( optIndex );
}


float SpringUnitSyncLib::GetOptionNumberMin( int optIndex )
{
	InitLib( m_get_option_number_min );

	return m_get_option_number_min( optIndex );
}


float SpringUnitSyncLib::GetOptionNumberMax( int optIndex )
{
	InitLib( m_get_option_number_max );

	return m_get_option_number_max( optIndex );
}


float SpringUnitSyncLib::GetOptionNumberStep( int optIndex )
{
	InitLib( m_get_option_number_step );

	return m_get_option_number_step( optIndex );
}


std::string SpringUnitSyncLib::GetOptionStringDef( int optIndex )
{
	InitLib( m_get_option_string_def );

	return m_get_option_string_def( optIndex );
}


int SpringUnitSyncLib::GetOptionStringMaxLen( int optIndex )
{
	InitLib( m_get_option_string_max_len );

	return m_get_option_string_max_len( optIndex );
}


int SpringUnitSyncLib::GetOptionListCount( int optIndex )
{
	InitLib( m_get_option_list_count );

	return m_get_option_list_count( optIndex );
}


std::string SpringUnitSyncLib::GetOptionListDef( int optIndex )
{
	InitLib( m_get_option_list_def );

	return m_get_option_list_def( optIndex );
}


std::string SpringUnitSyncLib::GetOptionListItemKey( int optIndex, int itemIndex )
{
	InitLib( m_get_option_list_item_key );

	return m_get_option_list_item_key( optIndex, itemIndex  );
}


std::string SpringUnitSyncLib::GetOptionListItemName( int optIndex, int itemIndex )
{
	InitLib( m_get_option_list_item_name );

	return m_get_option_list_item_name( optIndex, itemIndex  );
}


std::string SpringUnitSyncLib::GetOptionListItemDesc( int optIndex, int itemIndex )
{
	InitLib( m_get_option_list_item_desc );

	return m_get_option_list_item_desc( optIndex, itemIndex  );
}


int SpringUnitSyncLib::OpenArchive( const std::string& name )
{
	InitLib( m_open_archive );

	return m_open_archive( name.c_str() );
}


void SpringUnitSyncLib::CloseArchive( int archive )
{
	InitLib( m_close_archive );

	m_close_archive( archive );
}


int SpringUnitSyncLib::FindFilesArchive( int archive, int cur, std::string& nameBuf )
{
	InitLib( m_find_Files_archive );

	char buffer[1025];
	int size = 1024;
	bool ret = m_find_Files_archive( archive, cur, &buffer[0], &size );
	buffer[1024] = 0;
	nameBuf = &buffer[0];

	return ret;
}


int SpringUnitSyncLib::OpenArchiveFile( int archive, const std::string& name )
{
	InitLib( m_open_archive_file );

	return m_open_archive_file( archive, name.c_str() );
}


int SpringUnitSyncLib::ReadArchiveFile( int archive, int handle, void* buffer, int numBytes)
{
	InitLib( m_read_archive_file );

	return m_read_archive_file( archive, handle, buffer, numBytes );
}


void SpringUnitSyncLib::CloseArchiveFile( int archive, int handle )
{
	InitLib( m_close_archive_file );

	m_close_archive_file( archive, handle );
}


int SpringUnitSyncLib::SizeArchiveFile( int archive, int handle )
{
	InitLib( m_size_archive_file );

	return m_size_archive_file( archive, handle );
}


std::string SpringUnitSyncLib::GetArchivePath( const std::string& name )
{
	InitLib( m_get_archive_path );

	return m_get_archive_path( name.c_str() );
}


int SpringUnitSyncLib::GetSpringConfigInt( const std::string& key, int defValue )
{
	InitLib( m_get_spring_config_int );

	return m_get_spring_config_int( key.c_str(), defValue );
}


std::string SpringUnitSyncLib::GetSpringConfigString( const std::string& key, const std::string& defValue )
{
	InitLib( m_get_spring_config_string );

	return m_get_spring_config_string( key.c_str(), defValue.c_str() );
}


float SpringUnitSyncLib::GetSpringConfigFloat( const std::string& key, const float defValue )
{
	InitLib( m_get_spring_config_float );

	return m_get_spring_config_float( key.c_str(), defValue );
}


void SpringUnitSyncLib::SetSpringConfigString( const std::string& key, const std::string& value )
{
	InitLib( m_set_spring_config_string );

	m_set_spring_config_string( key.c_str(), value.c_str() );
}


void SpringUnitSyncLib::SetSpringConfigInt( const std::string& key, int value )
{
	InitLib( m_set_spring_config_int );

	m_set_spring_config_int( key.c_str(), value );
}


void SpringUnitSyncLib::SetSpringConfigFloat( const std::string& key, const float value )
{
	InitLib( m_set_spring_config_float );

	m_set_spring_config_float( key.c_str(), value );
}


int SpringUnitSyncLib::GetSkirmishAICount( const std::string& modname )
{
	InitLib( m_get_skirmish_ai_count );
	_SetCurrentMod( modname );

	return m_get_skirmish_ai_count();
}


SpringUnitSyncLib::StringVector SpringUnitSyncLib::GetAIInfo( int aiIndex )
{
	InitLib( m_get_skirmish_ai_count );
	CHECK_FUNCTION( m_get_skirmish_ai_info_count );
	CHECK_FUNCTION( m_get_skirmish_ai_info_description );
	CHECK_FUNCTION( m_get_skirmish_ai_info_key );
	CHECK_FUNCTION( m_get_skirmish_ai_info_value );

	StringVector ret;
	if ( !(( aiIndex >= 0 ) && ( aiIndex < m_get_skirmish_ai_count() )) )
		LSL_THROW( unitsync, "aiIndex out of bounds");

	int infoCount = m_get_skirmish_ai_info_count( aiIndex );
	for( int i = 0; i < infoCount; i++ )
	{
		ret.push_back( m_get_skirmish_ai_info_key( i ) );
		ret.push_back( m_get_skirmish_ai_info_value( i ) );
		ret.push_back( m_get_skirmish_ai_info_description( i ) );
	}
	return ret;
}

std::string SpringUnitSyncLib::GetArchiveChecksum( const std::string& VFSPath )
{
	InitLib( m_get_archive_checksum );
	return Util::ToString( m_get_archive_checksum( VFSPath.c_str() ) );
}

/// lua parser

void SpringUnitSyncLib::CloseParser()
{
	InitLib( m_parser_close );

	m_parser_close();
}

bool SpringUnitSyncLib::OpenParserFile( const std::string& filename, const std::string& filemodes, const std::string& accessModes )
{
	InitLib( m_parser_open_file );

	return m_parser_open_file( filename.c_str(), filemodes.c_str(), accessModes.c_str() );
}

bool SpringUnitSyncLib::OpenParserSource( const std::string& source, const std::string& accessModes )
{
	InitLib( m_parser_open_source );

	return m_parser_open_source( source.c_str(), accessModes.c_str() );
}

bool SpringUnitSyncLib::ParserExecute()
{
	InitLib( m_parser_execute );

	return m_parser_execute();
}

std::string SpringUnitSyncLib::ParserErrorLog()
{
	InitLib( m_parser_error_log );

	return m_parser_error_log();
}


void SpringUnitSyncLib::ParserAddTable( int key, bool override )
{
	InitLib( m_parser_add_table_int );

	m_parser_add_table_int( key, override );
}

void SpringUnitSyncLib::ParserAddTable( const std::string& key, bool override )
{
	InitLib( m_parser_add_table_string );

	m_parser_add_table_string( key.c_str(), override );
}

void SpringUnitSyncLib::ParserEndTable()
{
	InitLib( m_parser_end_table );

	m_parser_end_table();
}

void SpringUnitSyncLib::ParserAddTableValue( int key, int val )
{
	InitLib( m_parser_add_int_key_int_value );

	m_parser_add_int_key_int_value( key, val );
}

void SpringUnitSyncLib::ParserAddTableValue( const std::string& key, int val )
{
	InitLib( m_parser_add_string_key_int_value );

	m_parser_add_string_key_int_value( key.c_str(), val );
}

void SpringUnitSyncLib::ParserAddTableValue( int key, bool val )
{
	InitLib( m_parser_add_int_key_int_value );

	m_parser_add_int_key_int_value( key, val );
}

void SpringUnitSyncLib::ParserAddTableValue( const std::string& key, bool val )
{
	InitLib( m_parser_add_string_key_int_value );

	m_parser_add_string_key_int_value( key.c_str(), val );
}

void SpringUnitSyncLib::ParserAddTableValue( int key, const std::string& val )
{
	InitLib( m_parser_add_int_key_string_value );

	m_parser_add_int_key_string_value( key, val.c_str() );
}

void SpringUnitSyncLib::ParserAddTableValue( const std::string& key, const std::string& val )
{
	InitLib( m_parser_add_string_key_string_value );

	m_parser_add_string_key_string_value( key.c_str(), val.c_str() );
}

void SpringUnitSyncLib::ParserAddTableValue( int key, float val )
{
	InitLib( m_parser_add_int_key_float_value );

	m_parser_add_int_key_float_value( key, val );
}

void SpringUnitSyncLib::ParserAddTableValue( const std::string& key, float val )
{
	InitLib( m_parser_add_string_key_float_value );

	m_parser_add_string_key_float_value( key.c_str(), val );
}


bool SpringUnitSyncLib::ParserGetRootTable()
{
	InitLib( m_parser_root_table );

	return m_parser_root_table();
}

bool SpringUnitSyncLib::ParserGetRootTableExpression( const std::string& exp )
{
	InitLib( m_parser_root_table_expression );

	return m_parser_root_table_expression( exp.c_str() );
}

bool SpringUnitSyncLib::ParserGetSubTableInt( int key )
{
	InitLib( m_parser_sub_table_int );

	return m_parser_sub_table_int( key );
}

bool SpringUnitSyncLib::ParserGetSubTableString( const std::string& key )
{
	InitLib( m_parser_sub_table_string );

	return m_parser_sub_table_string( key.c_str() );
}

bool SpringUnitSyncLib::ParserGetSubTableInt( const std::string& exp )
{
	InitLib( m_parser_sub_table_expression );

	return m_parser_sub_table_expression( exp.c_str() );
}

void SpringUnitSyncLib::ParserPopTable()
{
	InitLib( m_parser_pop_table );

	m_parser_pop_table();
}


bool SpringUnitSyncLib::ParserKeyExists( int key )
{
	InitLib( m_parser_key_int_exists );

	return m_parser_key_int_exists( key );
}

bool SpringUnitSyncLib::ParserKeyExists( const std::string& key )
{
	InitLib( m_parser_key_string_exists );

	return m_parser_key_string_exists( key.c_str() );
}


int SpringUnitSyncLib::ParserGetKeyType( int key )
{
	InitLib( m_parser_int_key_get_type );

	return m_parser_int_key_get_type( key );
}

int SpringUnitSyncLib::ParserGetKeyType( const std::string& key )
{
	InitLib( m_parser_string_key_get_type );

	return m_parser_string_key_get_type( key.c_str() );
}


int SpringUnitSyncLib::ParserGetIntKeyListCount()
{
	InitLib( m_parser_int_key_get_list_count );

	return m_parser_int_key_get_list_count();
}

int SpringUnitSyncLib::ParserGetIntKeyListEntry( int index )
{
	InitLib( m_parser_int_key_get_list_entry );

	return m_parser_int_key_get_list_entry( index );
}

int SpringUnitSyncLib::ParserGetStringKeyListCount()
{
	InitLib( m_parser_string_key_get_list_count );

	return m_parser_string_key_get_list_count();
}

int SpringUnitSyncLib::ParserGetStringKeyListEntry( int index )
{
	InitLib( m_parser_int_key_get_list_entry );

	return m_parser_int_key_get_list_entry( index );
}


int SpringUnitSyncLib::GetKeyValue( int key, int defval )
{
	InitLib( m_parser_int_key_get_int_value );

	return m_parser_int_key_get_int_value( key, defval );
}

bool SpringUnitSyncLib::GetKeyValue( int key, bool defval )
{
	InitLib( m_parser_int_key_get_bool_value );

	return m_parser_int_key_get_bool_value( key, defval );
}

std::string SpringUnitSyncLib::GetKeyValue( int key, const std::string& defval )
{
	InitLib( m_parser_int_key_get_string_value );

	return m_parser_int_key_get_string_value( key, defval.c_str() ) );
}

float SpringUnitSyncLib::GetKeyValue( int key, float defval )
{
	InitLib( m_parser_int_key_get_float_value );

	return m_parser_int_key_get_float_value( key, defval );
}

int SpringUnitSyncLib::GetKeyValue( const std::string& key, int defval )
{
	InitLib( m_parser_string_key_get_int_value );

	return m_parser_string_key_get_int_value( key.c_str(), defval );
}

bool SpringUnitSyncLib::GetKeyValue( const std::string& key, bool defval )
{
	InitLib( m_parser_string_key_get_bool_value );
	return m_parser_string_key_get_bool_value( key.c_str(), defval );
}

std::string SpringUnitSyncLib::GetKeyValue( const std::string& key, const std::string& defval )
{
	InitLib( m_parser_string_key_get_string_value );
	return m_parser_string_key_get_string_value( key.c_str(), defval.c_str() );
}

float SpringUnitSyncLib::GetKeyValue( const std::string& key, float defval )
{
	InitLib( m_parser_string_key_get_float_value );
	return m_parser_string_key_get_float_value( key.c_str(), defval );
}

} //namespace LSL