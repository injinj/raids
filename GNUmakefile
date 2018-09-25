# defines a directory for build, for example, RH6_x86_64
lsb_dist     := $(shell if [ -x /usr/bin/lsb_release ] ; then lsb_release -is ; else echo Linux ; fi)
lsb_dist_ver := $(shell if [ -x /usr/bin/lsb_release ] ; then lsb_release -rs | sed 's/[.].*//' ; fi)
uname_m      := $(shell uname -m)

short_dist_lc := $(patsubst CentOS,rh,$(patsubst RedHat,rh,\
                   $(patsubst Fedora,fc,$(patsubst Ubuntu,ub,\
                     $(patsubst Debian,deb,$(patsubst SUSE,ss,$(lsb_dist)))))))
short_dist    := $(shell echo $(short_dist_lc) | tr a-z A-Z)
rpm_os        := $(short_dist_lc)$(lsb_dist_ver).$(uname_m)

# this is where the targets are compiled
build_dir ?= $(short_dist)$(lsb_dist_ver)_$(uname_m)$(port_extra)
bind      := $(build_dir)/bin
libd      := $(build_dir)/lib64
objd      := $(build_dir)/obj
dependd   := $(build_dir)/dep

# use 'make port_extra=-g' for debug build
ifeq (-g,$(findstring -g,$(port_extra)))
  DEBUG = true
endif

CC          ?= gcc
CXX         ?= g++
cc          := $(CC)
cpp         := $(CXX)
cppflags    := -fno-rtti -fno-exceptions
arch_cflags := -march=corei7-avx -fno-omit-frame-pointer
cpplink     := gcc
gcc_wflags  := -Wall -Wextra -Werror
fpicflags   := -fPIC
soflag      := -shared

ifdef DEBUG
default_cflags := -ggdb
else
default_cflags := -ggdb -O3
endif
# rpmbuild uses RPM_OPT_FLAGS
CFLAGS ?= $(default_cflags)
#RPM_OPT_FLAGS ?= $(default_cflags)
#CFLAGS ?= $(RPM_OPT_FLAGS)
cflags := $(gcc_wflags) $(CFLAGS) $(arch_cflags)

# where to find the raids/xyz.h files
INCLUDES    ?= -Iinclude -Iraikv/include -Ih3/src/h3lib/include -Ilibdecnumber
includes    := $(INCLUDES)
DEFINES     ?=
defines     := $(DEFINES)
cpp_lnk     :=
sock_lib    :=
math_lib    := -lm
thread_lib  := -pthread -lrt
h3_lib      := h3/lib/libh3.a
dec_lib     := libdecnumber/libdecnumber.a
kv_lib      := raikv/$(libd)/libraikv.a
lnk_lib     := $(dec_lib) $(h3_lib) $(kv_lib) -lpcre2-8 -lcrypto
malloc_lib  :=

# targets filled in below
all_exes    :=
all_libs    :=
all_dlls    :=
all_depends :=
gen_files   :=
major_num   := 1
minor_num   := 0
patch_num   := 0
build_num   := 1
version     := $(major_num).$(minor_num).$(patch_num)
ver_build   := $(version)-$(build_num)
defines     := -DDS_VER=$(ver_build)

.PHONY: everything
everything: $(kv_lib) $(h3_lib) $(dec_lib) all

libraids_files := ev_net ev_service ev_http ev_client ev_tcp ev_unix ev_nats \
                  stream_buf route_db redis_msg redis_cmd_db redis_exec \
		  redis_geo redis_hash redis_hyperloglog redis_key redis_list \
		  redis_pubsub redis_script redis_set redis_sortedset \
		  redis_stream redis_string redis_transaction decimal
libraids_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(libraids_files)))
libraids_dbjs  := $(addprefix $(objd)/, $(addsuffix .fpic.o, $(libraids_files)))
libraids_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(libraids_files))) \
                  $(addprefix $(dependd)/, $(addsuffix .fpic.d, $(libraids_files)))
libraids_dlnk  := $(lnk_lib)
libraids_spec  := $(version)-$(build_num)
libraids_ver   := $(major_num).$(minor_num)

$(libd)/libraids.a: $(libraids_objs)
$(libd)/libraids.so: $(libraids_dbjs)

all_libs    += $(libd)/libraids.a
# $(libd)/libraids.so
all_depends += $(libraids_deps)

server_files := emain
server_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(server_files)))
server_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(server_files)))
server_libs  := $(libd)/libraids.a
server_lnk   := $(server_libs) $(lnk_lib)

$(bind)/server: $(server_objs) $(server_libs)

client_files := cli
client_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(client_files)))
client_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(client_files)))
client_libs  := $(libd)/libraids.a
client_lnk   := $(client_libs) $(lnk_lib)

$(bind)/client: $(client_objs) $(client_libs)

test_msg_files := test_msg
test_msg_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(test_msg_files)))
test_msg_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(test_msg_files)))
test_msg_libs  := $(libd)/libraids.a
test_msg_lnk   := $(test_msg_libs) $(lnk_lib)

$(bind)/test_msg: $(test_msg_objs) $(test_msg_libs)

test_cmd_files := test_cmd
test_cmd_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(test_cmd_files)))
test_cmd_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(test_cmd_files)))
test_cmd_libs  := $(libd)/libraids.a
test_cmd_lnk   := $(test_cmd_libs) $(lnk_lib)

$(bind)/test_cmd: $(test_cmd_objs) $(test_cmd_libs)

redis_cmd_files := redis_cmd redis_msg
redis_cmd_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(redis_cmd_files)))
redis_cmd_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(redis_cmd_files)))
redis_cmd_lnk   := $(lnk_lib)

$(bind)/redis_cmd: $(redis_cmd_objs) $(redis_cmd_libs)

test_list_files := test_list
test_list_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(test_list_files)))
test_list_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(test_list_files)))
test_list_libs  := $(libd)/libraids.a
test_list_lnk   := $(test_list_libs) $(lnk_lib)

$(bind)/test_list: $(test_list_objs) $(test_list_libs)

test_hash_files := test_hash
test_hash_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(test_hash_files)))
test_hash_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(test_hash_files)))
test_hash_libs  := $(libd)/libraids.a
test_hash_lnk   := $(test_hash_libs) $(lnk_lib)

$(bind)/test_hash: $(test_hash_objs) $(test_hash_libs)

test_set_files := test_set
test_set_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(test_set_files)))
test_set_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(test_set_files)))
test_set_libs  := $(libd)/libraids.a
test_set_lnk   := $(test_set_libs) $(lnk_lib)

$(bind)/test_set: $(test_set_objs) $(test_set_libs)

test_zset_files := test_zset
test_zset_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(test_zset_files)))
test_zset_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(test_zset_files)))
test_zset_libs  := $(libd)/libraids.a
test_zset_lnk   := $(test_zset_libs) $(lnk_lib)

$(bind)/test_zset: $(test_zset_objs) $(test_zset_libs)

test_hllnum_files := test_hllnum
test_hllnum_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(test_hllnum_files)))
test_hllnum_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(test_hllnum_files)))
test_hllnum_lnk   := $(test_hllnum_libs) $(lnk_lib)

$(bind)/test_hllnum: $(test_hllnum_objs) $(test_hllnum_libs)

test_hllw_files := test_hllw
test_hllw_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(test_hllw_files)))
test_hllw_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(test_hllw_files)))
test_hllw_lnk   := $(test_hllw_libs) $(lnk_lib)

$(bind)/test_hllw: $(test_hllw_objs) $(test_hllw_libs)

test_hllsub_files := test_hllsub
test_hllsub_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(test_hllsub_files)))
test_hllsub_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(test_hllsub_files)))
test_hllsub_lnk   := $(test_hllsub_libs) $(lnk_lib)

$(bind)/test_hllsub: $(test_hllsub_objs) $(test_hllsub_libs)

test_geo_files := test_geo
test_geo_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(test_geo_files)))
test_geo_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(test_geo_files)))
test_geo_libs  := $(libd)/libraids.a
test_geo_lnk   := $(test_geo_libs) $(lnk_lib)

$(bind)/test_geo: $(test_geo_objs) $(test_geo_libs)

test_routes_files := test_routes
test_routes_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(test_routes_files)))
test_routes_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(test_routes_files)))
test_routes_libs  := $(libd)/libraids.a
test_routes_lnk   := $(test_routes_libs) $(lnk_lib)

$(bind)/test_routes: $(test_routes_objs) $(test_routes_libs)

test_delta_files := test_delta
test_delta_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(test_delta_files)))
test_delta_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(test_delta_files)))
test_delta_libs  := $(libd)/libraids.a
test_delta_lnk   := $(test_delta_libs) $(lnk_lib)

$(bind)/test_delta: $(test_delta_objs) $(test_delta_libs)

test_decimal_files := test_decimal
test_decimal_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(test_decimal_files)))
test_decimal_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(test_decimal_files)))
test_decimal_libs  := $(libd)/libraids.a
test_decimal_lnk   := $(test_decimal_libs) $(lnk_lib)

$(bind)/test_decimal: $(test_decimal_objs) $(test_decimal_libs)

all_exes    += $(bind)/server $(bind)/client $(bind)/test_msg \
               $(bind)/redis_cmd $(bind)/test_cmd $(bind)/test_list \
	       $(bind)/test_hash $(bind)/test_set $(bind)/test_zset \
	       $(bind)/test_hllnum $(bind)/test_hllw $(bind)/test_hllsub \
	       $(bind)/test_geo $(bind)/test_routes $(bind)/test_delta \
	       $(bind)/test_decimal
all_depends += $(server_deps) $(client_deps) $(test_msg_deps) \
               $(redis_cmd_deps) $(test_cmd_deps) $(test_list_deps) \
	       $(test_hash_deps) $(test_set_deps) $(test_zset_deps) \
	       $(test_hllnum_deps) $(test_hllw_deps) $(test_hllsub_deps) \
	       $(test_geo_deps) $(test_routes_deps) $(test_delta_deps) \
	       $(test_decimal_deps)

all_dirs := $(bind) $(libd) $(objd) $(dependd)

include/raids/redis_cmd.h: $(bind)/redis_cmd
	$(bind)/redis_cmd > include/raids/redis_cmd.h
gen_files += include/raids/redis_cmd.h

$(kv_lib):
	(cd raikv ; make)

$(h3_lib):
	(cd h3 ; cmake . ; make)

$(dec_lib):
	(cd libdecnumber ; make)

# the default targets
.PHONY: all
all: $(gen_files) $(all_libs) $(all_dlls) $(all_exes)

# create directories
$(dependd):
	@mkdir -p $(all_dirs)

# remove target bins, objs, depends
.PHONY: clean
clean:
	rm -r -f $(bind) $(libd) $(objd) $(dependd)
	if [ "$(build_dir)" != "." ] ; then rmdir $(build_dir) ; fi

# force a remake of depend using 'make -B depend'
.PHONY: depend
depend: $(dependd)/depend.make

$(dependd)/depend.make: $(dependd) $(all_depends)
	@echo "# depend file" > $(dependd)/depend.make
	@cat $(all_depends) >> $(dependd)/depend.make

.PHONY: dist_bins
dist_bins: $(all_libs) $(bind)/server

# dependencies made by 'make depend'
-include $(dependd)/depend.make

$(objd)/%.o: src/%.cpp
	$(cpp) $(cflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.o: src/%.c
	$(cc) $(cflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.fpic.o: src/%.cpp
	$(cpp) $(cflags) $(fpicflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.fpic.o: src/%.c
	$(cc) $(cflags) $(fpicflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.o: test/%.cpp
	$(cpp) $(cflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.o: test/%.c
	$(cc) $(cflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(libd)/%.a:
	ar rc $@ $($(*)_objs)

$(libd)/%.so:
	$(cpplink) $(soflag) $(cflags) -o $@.$($(*)_spec) -Wl,-soname=$(@F).$($(*)_ver) $($(*)_dbjs) $($(*)_dlnk) $(cpp_dll_lnk) $(sock_lib) $(math_lib) $(thread_lib) $(malloc_lib) $(dynlink_lib) && \
	cd $(libd) && ln -f -s $(@F).$($(*)_spec) $(@F).$($(*)_ver) && ln -f -s $(@F).$($(*)_ver) $(@F)

$(bind)/%:
	$(cpplink) $(cflags) -o $@ $($(*)_objs) -L$(libd) $($(*)_lnk) $(cpp_lnk) $(sock_lib) $(math_lib) $(thread_lib) $(malloc_lib) $(dynlink_lib)

$(dependd)/%.d: src/%.cpp
	gcc -x c++ $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).o -MF $@

$(dependd)/%.d: src/%.c
	gcc $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).o -MF $@

$(dependd)/%.fpic.d: src/%.cpp
	gcc -x c++ $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).fpic.o -MF $@

$(dependd)/%.fpic.d: src/%.c
	gcc $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).fpic.o -MF $@

$(dependd)/%.d: test/%.cpp
	gcc -x c++ $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).o -MF $@

$(dependd)/%.d: test/%.c
	gcc $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).o -MF $@

