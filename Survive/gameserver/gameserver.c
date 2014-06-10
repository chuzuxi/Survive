#include "kendynet.h"
#include "gameserver.h"
#include "config.h"
#include "lua/lua_util.h"
#include "kn_stream_conn_server.h"
#include "kn_stream_conn_client.h"
#include "common/netcmd.h"
#include "common/cmdhandler.h"
#include "common/common_c_function.h"

IMP_LOG(gamelog);

#define MAXCMD 65535
static cmd_handler_t handler[MAXCMD] = {NULL};

static kn_stream_client_t c;
static kn_stream_conn_t   togrp;

static int on_gate_packet(kn_stream_conn_t con,rpacket_t rpk){
	uint16_t cmd = rpk_read_uint16(rpk);
	if(handler[cmd]){
		lua_State *L = handler[cmd]->obj->L;
		if(CALL_OBJ_FUNC2(handler[cmd]->obj,"handle",0,
						  lua_pushlightuserdata(L,rpk),
						  lua_pushlightuserdata(L,con))){
			const char * error = lua_tostring(L, -1);
			lua_pop(L,1);
			LOG_GAME(LOG_INFO,"error on handle[%u]:%s\n",cmd,error);
		}
	}
	return 1;
}

static void on_gate_disconnected(kn_stream_conn_t conn,int err){

}

static void on_new_gate(kn_stream_server_t server,kn_stream_conn_t conn){
	if(0 == kn_stream_server_bind(server,conn,0,65536,
				      on_gate_packet,on_gate_disconnected,
				      30*1000,NULL,0,NULL)){
	}else{
		kn_stream_conn_close(conn);
	}
}


static int on_group_packet(kn_stream_conn_t con,rpacket_t rpk){
	uint16_t cmd = rpk_read_uint16(rpk);
	if(handler[cmd]){
		lua_State *L = handler[cmd]->obj->L;
		if(CALL_OBJ_FUNC2(handler[cmd]->obj,"handle",0,
						  lua_pushlightuserdata(L,rpk),
						  lua_pushlightuserdata(L,con))){
			const char * error = lua_tostring(L, -1);
			lua_pop(L,1);
			LOG_GAME(LOG_INFO,"error on handle[%u]:%s\n",cmd,error);
		}
	}
	return 1;
}

static void on_group_connect_failed(kn_stream_client_t _,kn_sockaddr *addr,int err,void *ud)
{
	(void)_;
	kn_stream_connect(c,NULL,addr,NULL);
}

static void on_group_disconnected(kn_stream_conn_t conn,int err){
		
}

static void on_group_connect(kn_stream_client_t _,kn_stream_conn_t conn,void *ud){
	(void)_;
	if(0 == kn_stream_client_bind(c,conn,0,65536,on_group_packet,on_group_disconnected,
						  30*1000,NULL,0,NULL)){
	
		togrp = conn;
	}else{
		
		LOG_GAME(LOG_ERROR,"on_group_connect failed\n");
	}
}

static int reg_cmd_handler(lua_State *L){
	uint16_t cmd = lua_tonumber(L,1);
	luaObject_t obj = create_luaObj(L,2);
	if(!handler[cmd]){
		cmd_handler_t h = calloc(1,sizeof(*handler));
		h->obj = obj;
		handler[cmd] = h;
		lua_pushboolean(L,1);
	}else{
		release_luaObj(obj);
		lua_pushboolean(L,0);
	}
	return 1;
}

static int lua_send2grp(lua_State *L){
	wpacket_t wpk = lua_touserdata(L,1);
	if(!togrp){
		wpk_destroy(wpk);
	}else{
		kn_stream_conn_send(togrp,wpk);
	}
	return 0;
}


void reg_game_c_function(lua_State *L){
	lua_getglobal(L,"GameApp");
	if(!lua_istable(L, -1))
	{
		lua_pop(L,1);
		lua_newtable(L);
		lua_pushvalue(L,-1);
		lua_setglobal(L,"GameApp");
	}

	lua_pushstring(L, "reg_cmd_handler");
	lua_pushcfunction(L, &reg_cmd_handler);
	lua_settable(L, -3);

	lua_pushstring(L, "send2grp");
	lua_pushcfunction(L, &lua_send2grp);
	lua_settable(L, -3);

	lua_pop(L,1);
}

static lua_State *init(){
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	if (luaL_dofile(L,"script/handler.lua")) {
		const char * error = lua_tostring(L, -1);
		lua_pop(L,1);
		LOG_GAME(LOG_INFO,"error on handler.lua:%s\n",error);
		lua_close(L); 
		return NULL;
	}
	//注册C函数，常量到lua
	reg_common_c_function(L);

	//注册group特有的函数
	reg_game_c_function(L);

	//注册lua消息处理器
	if(CALL_LUA_FUNC(L,"reghandler",0)){
		const char * error = lua_tostring(L, -1);
		lua_pop(L,1);
		LOG_GAME(LOG_INFO,"error on reghandler:%s\n",error);
		lua_close(L); 
	}
	return L;
}

static volatile int stop = 0;
static void sig_int(int sig){
	stop = 1;
}


int main(int argc,char **argv){

	if(loadconfig() != 0){
		return 0;
	}

	if(!init())
		return 0;

	signal(SIGINT,sig_int);
	kn_proactor_t p = kn_new_proactor();
	//启动监听
	kn_sockaddr lgateserver;
	kn_addr_init_in(&lgateserver,kn_to_cstr(g_config->lgateip),g_config->lgateport);
	kn_new_stream_server(p,&lgateserver,on_new_gate);

	//连接group
	kn_stream_client_t c = kn_new_stream_client(p,
									on_group_connect,
									on_group_connect_failed);

	kn_sockaddr grpaddr;
	kn_addr_init_in(&grpaddr,kn_to_cstr(g_config->groupip),g_config->groupport);
	kn_stream_connect(c,NULL,&grpaddr,NULL);
	while(!stop)
		kn_proactor_run(p,50);

	return 0;	
}



