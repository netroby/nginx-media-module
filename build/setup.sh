#!/bin/bash
#set -x
set -o nounset
ALLMEDIAVERSION="AllMedia 0.1.1"
CURRENT_PATH=`pwd`
cd ${CURRENT_PATH}/..
export ALLMEDIA_ROOT=$PWD
export PREFIX_ROOT=/home/allmedia/
export THIRD_ROOT=${CURRENT_PATH}/3rd_party/
export EXTEND_ROOT=${CURRENT_PATH}/extend/
export PATCH_ROOT=${CURRENT_PATH}/patch/
export SCRIPT_ROOT=${CURRENT_PATH}/script/
echo "------------------------------------------------------------------------------"
echo " ALLMEDIA_ROOT exported as ${ALLMEDIA_ROOT}"
echo "------------------------------------------------------------------------------"

WITHDEBUG="--with-debug"
#WITHDEBUG=""
#NGX_LINK="--add-dynamic-module"
NGX_LINK="--add-module"
#
# Sets QUIT variable so script will finish.
#
quit()
{
    QUIT=$1
}

build_libxml2()
{
    module_pack="libxml2-2.9.7.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the libxml2 package from server\n"
        wget ftp://xmlsoft.org/libxml2/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd libxml2*
    ./configure --prefix=${PREFIX_ROOT} --enable-shared=no --with-sax1 --with-zlib=${PREFIX_ROOT} --with-iconv=${PREFIX_ROOT}
                
    if [ 0 -ne ${?} ]; then
        echo "configure libxml2 fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build libxml2 fail!\n"
        return 1
    fi
    
    return 0
}

build_cjson()
{
    module_pack="cJSON-master.zip"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the libxml2 package from server\n"
        wget https://github.com/DaveGamble/cJSON/archive/master.zip -O ${module_pack}
    fi
    unzip -o ${module_pack}
    
    cd cJSON*
    
    make PREFIX=${EXTEND_ROOT} && make install PREFIX=${EXTEND_ROOT}
                
    if [ 0 -ne ${?} ]; then
        echo "build cjson fail!\n"
        return 1
    fi
    cp -f libcjson.a ${EXTEND_ROOT}/lib/
    
    return 0
}

build_pcre()
{
    module_pack="pcre-8.39.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the pcre package from server\n"
        wget https://sourceforge.net/projects/pcre/files/pcre/8.39/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd pcre*
    ./configure --prefix=${EXTEND_ROOT} 
                
    if [ 0 -ne ${?} ]; then
        echo "configure pcre fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build pcre fail!\n"
        return 1
    fi
    
    return 0
}

build_zlib()
{
    module_pack="zlib-1.2.8.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the zlib package from server\n"
        wget http://zlib.net/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd zlib*
    ./configure --prefix=${EXTEND_ROOT} 
                
    if [ 0 -ne ${?} ]; then
        echo "configure zlib fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build zlib fail!\n"
        return 1
    fi
    
    return 0
}

build_libiconv()
{
    module_pack="libiconv-1.14.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the libiconv package from server\n"
        wget http://ftp.gnu.org/pub/gnu/libiconv/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd libiconv*
    patch -p0 <${PATCH_ROOT}/libiconv.patch
    ./configure --prefix=${EXTEND_ROOT} --enable-static=yes
                
    if [ 0 -ne ${?} ]; then
        echo "configure libiconv fail!\n"
        return 1
    fi
    
    make clean  
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build libiconv fail!\n"
        return 1
    fi
    
    return 0
}

build_xzutils()
{
    module_pack="xz-5.2.2.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the xzutils package from server\n"
        wget http://tukaani.org/xz/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd xz*
    ./configure --prefix=${EXTEND_ROOT} 
                
    if [ 0 -ne ${?} ]; then
        echo "configure xzutils fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build xzutils fail!\n"
        return 1
    fi
    
    return 0
}

build_bzip2()
{
    module_pack="bzip2-1.0.6.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the bzip2 package from server\n"
        wget http://www.bzip.org/1.0.6/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd bzip2*
    #./configure --prefix=${EXTEND_ROOT}
    EXTEND_ROOT_SED=$(echo ${EXTEND_ROOT} |sed -e 's/\//\\\//g')
    sed -i "s/PREFIX\=\/usr\/local/PREFIX\=${EXTEND_ROOT_SED}/" Makefile    
    sed -i "s/CFLAGS=-Wall -Winline -O2 -g/CFLAGS\=-Wall -Winline -O2 -fPIC -g/" Makefile
    if [ 0 -ne ${?} ]; then
        echo "sed bzip2 fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build bzip2 fail!\n"
        return 1
    fi
    
    return 0
}


build_openssl()
{
    module_pack="openssl-0.9.8w.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the openssl package from server\n"
        wget https://www.openssl.org/source/old/0.9.x/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd openssl*
                    
    if [ 0 -ne ${?} ]; then
        echo "get openssl fail!\n"
        return 1
    fi
    
    return 0
}

build_zookeeper()
{
    module_pack="zookeeper-3.4.9.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the zookeeper package from server\n"
        wget https://mirrors.tuna.tsinghua.edu.cn/apache/zookeeper/zookeeper-3.4.9/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd zookeeper*
    cd src/c
    
    ./configure --prefix=${EXTEND_ROOT} 
                
    if [ 0 -ne ${?} ]; then
        echo "configure zookeepr fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build zookeepr fail!\n"
        return 1
    fi
    
    return 0
}



build_curl_module()
{
    module_pack="curl-7.52.1.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the curl package from server\n"
        wget https://curl.haxx.se/download/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd curl*
    
    ./configure --prefix=${EXTEND_ROOT} 
                
    if [ 0 -ne ${?} ]; then
        echo "configure curl fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build curl fail!\n"
        return 1
    fi
    
    return 
}

build_minixml_module()
{
    module_pack="mxml-2.10.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the minixml package from server\n"
        wget http://www.msweet.org/files/project3/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd mxml*
    
    ./configure --prefix=${EXTEND_ROOT} 
                
    if [ 0 -ne ${?} ]; then
        echo "configure minixml fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build minixml fail!\n"
        return 1
    fi
    
    return 0
}

build_apr_util_module()
{
    module_pack="apr-util-1.5.4.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the apr-util package from server\n"
        wget http://mirrors.cnnic.cn/apache//apr/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd apr-util*
    
    ./configure --prefix=${EXTEND_ROOT} --with-apr=${EXTEND_ROOT} 
                
    if [ 0 -ne ${?} ]; then
        echo "configure apr-util fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build apr-util fail!\n"
        return 1
    fi
    
    return 0
}

build_apr_module()
{
    module_pack="apr-1.5.2.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the apr package from server\n"
        wget http://mirrors.cnnic.cn/apache//apr/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd apr-1*
    
    ./configure --prefix=${EXTEND_ROOT} 
                
    if [ 0 -ne ${?} ]; then
        echo "configure apr fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build apr fail!\n"
        return 1
    fi
    
    return 0
}

build_oss_sdk_module()
{
    module_pack="aliyun-oss-c-sdk-3.3.0.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the aliyun-oss-c-sdk package from server\n"
        wget https://github.com/aliyun/aliyun-oss-c-sdk/archive/3.3.0.tar.gz -O ${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd aliyun-oss-c-sdk*
    
    ##./configure --prefix=${EXTEND_ROOT} 
    cmake -f CMakeLists.txt -DCMAKE_BUILD_TYPE=Release -DCURL_INCLUDE_DIR=${EXTEND_ROOT}/include/curl/ -DCURL_LIBRARY=${EXTEND_ROOT}/lib/libcurl.so -DAPR_INCLUDE_DIR=${EXTEND_ROOT}/include/apr-1/ -DAPR_LIBRARY=${EXTEND_ROOT}/lib/libapr-1.so -DAPR_UTIL_INCLUDE_DIR=${EXTEND_ROOT}/include/apr-1 -DAPR_UTIL_LIBRARY=${EXTEND_ROOT}/lib/libaprutil-1.so -DMINIXML_INCLUDE_DIR=${EXTEND_ROOT}/include -DMINIXML_LIBRARY=${EXTEND_ROOT}/lib/libmxml.so -DCMAKE_INSTALL_PREFIX=${EXTEND_ROOT}
                
    if [ 0 -ne ${?} ]; then
        echo "configure aliyun-oss-c-sdk fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build aliyun-oss-c-sdk fail!\n"
        return 1
    fi
    
    return 0
}

build_extend_modules()
{
    build_bzip2
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_zlib
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_pcre
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_libiconv
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_libxml2
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_cjson
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_xzutils
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_openssl
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_zookeeper
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_minixml_module
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_curl_module
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_apr_module
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    ##apr util must build after apr
    build_apr_util_module
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_oss_sdk_module
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    return 0
}

build_mk_module()
{   
    module_pack="libMediakenerl-master.zip"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the libxml2 package from server\n"
        wget https://github.com/H-kernel/libMediakenerl/archive/master.zip -O ${module_pack}
    fi
    unzip -o ${module_pack}
    
    cd libMediakenerl*
    cd build/linux/
    chmod +x setup.sh
    ./setup.sh -p ${EXTEND_ROOT} -t ${THIRD_ROOT}
    if [ 0 -ne ${?} ]; then
        echo "build the media kernel module fail!\n"
        return 1
    fi
    
    return 0
}

build_engine_module()
{
    cd ${THIRD_ROOT}/nginx-*
    
    make&&make install
    
    if [ 0 -ne ${?} ]; then
       echo "make the nginx fail!\n"
       return 1
    fi
    echo "make the nginx success!\n"
    return 0
}

rebuild_engine_module()
{

    ###wget the nginx
    cd ${THIRD_ROOT}
    module_pack="nginx-1.14.0.tar.gz"
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the nginx package from server\n"
        if [ ! -x "`which wget 2>/dev/null`" ]; then
            echo "Need to install wget."
            return 1
        fi
        wget http://nginx.org/download/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    
    basic_opt=" --prefix=${PREFIX_ROOT} ${WITHDEBUG} 
                --with-threads 
                --with-file-aio 
                --with-ipv6
                --with-http_ssl_module 
                --with-http_realip_module 
                --with-http_addition_module 
                --with-http_sub_module 
                --with-http_dav_module 
                --with-http_flv_module 
                --with-http_mp4_module 
                --with-http_gunzip_module 
                --with-http_gzip_static_module 
                --with-http_random_index_module 
                --with-http_secure_link_module 
                --with-http_stub_status_module 
                --with-http_auth_request_module 
                --with-mail 
                --with-mail_ssl_module 
                --with-cc-opt=-O3 "
                
    
    third_opt=""
    cd ${THIRD_ROOT}/pcre*
    if [ 0 -eq ${?} ]; then
        third_opt="${third_opt} 
                    --with-pcre=`pwd`"
    fi
    cd ${THIRD_ROOT}/zlib*
    if [ 0 -eq ${?} ]; then
        third_opt="${third_opt}
                    --with-zlib=`pwd`"
    fi
    cd ${THIRD_ROOT}/openssl*
    if [ 0 -eq ${?} ]; then
        third_opt="${third_opt} 
                    --with-openssl=`pwd`"
    fi

    
    module_opt="" 
    cd ${ALLMEDIA_ROOT}
    if [ 0 -eq ${?} ]; then
        module_opt="${module_opt} 
                     --add-module=`pwd`"
        LD_LIBRARY_PATH=${EXTEND_ROOT}/lib
        LIBRARY_PATH=${EXTEND_ROOT}/lib
        C_INCLUDE_PATH=${EXTEND_ROOT}/include
        export LD_LIBRARY_PATH LIBRARY_PATH C_INCLUDE_PATH
    fi
        
    all_opt="${basic_opt} ${third_opt} ${module_opt}"
    
    echo "all optiont info:\n ${all_opt}"
    
    cd ${THIRD_ROOT}/nginx-1.*
    ./configure ${all_opt} 

    if [ 0 -ne ${?} ]; then
       echo "configure the nginx fail!\n"
       return 1
    fi
    
    make&&make install
    
    if [ 0 -ne ${?} ]; then
       echo "make the nginx fail!\n"
       return 1
    fi
    cp ${SCRIPT_ROOT}/start ${PREFIX_ROOT}/sbin
    cp ${SCRIPT_ROOT}/restart ${PREFIX_ROOT}/sbin
    cp ${SCRIPT_ROOT}/stop ${PREFIX_ROOT}/sbin
    chmod +x ${PREFIX_ROOT}/sbin/start
    chmod +x ${PREFIX_ROOT}/sbin/restart
    chmod +x ${PREFIX_ROOT}/sbin/stop
    cp ${SCRIPT_ROOT}/nginx.conf ${PREFIX_ROOT}/conf
    echo "make the nginx success!\n"
    cd ${ALLMEDIA_ROOT}
    return 0
}


build_allmedia_module()
{
    cd ${THIRD_ROOT}/allmedia*
    
    make&&make install
    
    if [ 0 -ne ${?} ]; then
       echo "make the allmedia fail!\n"
       return 1
    fi
    echo "make the allmedia success!\n"
    return 0
}

rebuild_allmedia_module()
{

    ###wget the allmedia
    cd ${THIRD_ROOT}
    module_pack="allmedia.zip"
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the allmedia package from server\n"
        if [ ! -x "`which wget 2>/dev/null`" ]; then
            echo "Need to install wget."
            return 1
        fi
        wget https://github.com/H-kernel/allmedia/archive/master.zip -O ${module_pack}
    fi
    unzip -o ${module_pack}
    
    
    basic_opt=" --prefix=${PREFIX_ROOT} ${WITHDEBUG} 
                --sbin-path=sbin/allmedia
                --with-threads 
                --with-file-aio 
                --with-ipv6
                --with-http_ssl_module 
                --with-http_realip_module 
                --with-http_addition_module 
                --with-http_sub_module 
                --with-http_dav_module 
                --with-http_flv_module 
                --with-http_mp4_module 
                --with-http_gunzip_module 
                --with-http_gzip_static_module 
                --with-http_random_index_module 
                --with-http_secure_link_module 
                --with-http_stub_status_module 
                --with-http_auth_request_module 
                --with-mail 
                --with-mail_ssl_module 
                --with-cc-opt=-O3 "
                
    
    third_opt=""
    cd ${THIRD_ROOT}/pcre*
    if [ 0 -eq ${?} ]; then
        third_opt="${third_opt} 
                    --with-pcre=`pwd`"
    fi
    cd ${THIRD_ROOT}/zlib*
    if [ 0 -eq ${?} ]; then
        third_opt="${third_opt}
                    --with-zlib=`pwd`"
    fi
    cd ${THIRD_ROOT}/openssl*
    if [ 0 -eq ${?} ]; then
        third_opt="${third_opt} 
                    --with-openssl=`pwd`"
    fi

    
    module_opt="" 
    cd ${ALLMEDIA_ROOT}
    if [ 0 -eq ${?} ]; then
        module_opt="${module_opt} 
                     --add-module=`pwd`"
        LD_LIBRARY_PATH=${EXTEND_ROOT}/lib
        LIBRARY_PATH=${EXTEND_ROOT}/lib
        C_INCLUDE_PATH=${EXTEND_ROOT}/include
        export LD_LIBRARY_PATH LIBRARY_PATH C_INCLUDE_PATH
    fi
        
    all_opt="${basic_opt} ${third_opt} ${module_opt}"
    
    echo "all optiont info:\n ${all_opt}"
    
    cd ${THIRD_ROOT}/allmedia*
    ./configure ${all_opt} 

    if [ 0 -ne ${?} ]; then
       echo "configure the allmedia fail!\n"
       return 1
    fi
    
    make&&make install
    
    if [ 0 -ne ${?} ]; then
       echo "make the allmedia fail!\n"
       return 1
    fi
    cp ${SCRIPT_ROOT}/start ${PREFIX_ROOT}/sbin
    cp ${SCRIPT_ROOT}/restart ${PREFIX_ROOT}/sbin
    cp ${SCRIPT_ROOT}/stop ${PREFIX_ROOT}/sbin
    chmod +x ${PREFIX_ROOT}/sbin/start
    chmod +x ${PREFIX_ROOT}/sbin/restart
    chmod +x ${PREFIX_ROOT}/sbin/stop
    cp ${SCRIPT_ROOT}/allmedia.conf ${PREFIX_ROOT}/conf
    echo "make the allmedia success!\n"
    cd ${ALLMEDIA_ROOT}
    return 0
}

clean_all()
{
    cd ${PREFIX_ROOT}/
    if [ 0 -eq ${?} ]; then
       rm -rf ./*
    fi
    
    cd ${THIRD_ROOT}/
    if [ 0 -eq ${?} ]; then
       rm -rf ./*
    fi
    cd ${EXTEND_ROOT}/
    if [ 0 -eq ${?} ]; then
       rm -rf ./*
    fi
    echo "clean all success!\n"
}

package_all()
{
    cp ${SCRIPT_ROOT}/run ${PREFIX_ROOT}/sbin
    cp ${SCRIPT_ROOT}/nginx.conf ${PREFIX_ROOT}/conf
    mkdir ${PREFIX_ROOT}/html/
    cp -R ${SCRIPT_ROOT}/html/* ${PREFIX_ROOT}/html/
    
    cd ${CURRENT_PATH}
    tar -zcvf allmedia.tar.gz allmedia/
    echo "package all success!\n"
}
setup()
{
        
    build_extend_modules
    if [ 0 -ne ${?} ]; then
        return
    fi 
    build_mk_module
    if [ 0 -ne ${?} ]; then
        return
    fi
    rebuild_engine_module
    if [ 0 -ne ${?} ]; then
        return
    fi
    rebuild_allmedia_module
    if [ 0 -ne ${?} ]; then
        return
    fi
    echo "make the all modules success!\n"
    cd ${ALLMEDIA_ROOT}
}

all_engine_func()
{
        TITLE="Setup the engine module"

        TEXT[1]="rebuild the engine module"
        FUNC[1]="rebuild_engine_module"
        
        TEXT[2]="build the engine module"
        FUNC[2]="build_engine_module"
}

all_media_func()
{
        TITLE="Setup the allmedia module"

        TEXT[1]="rebuild the allmedia module"
        FUNC[1]="rebuild_allmedia_module"
        
        TEXT[2]="build the allmedia module"
        FUNC[2]="build_allmedia_module"
}

all_modules_func()
{
        TITLE="Setup all the 3rd module"

        TEXT[1]="build the 3rd module"
        FUNC[1]="build_extend_modules"
        
        TEXT[2]="build the media kenerl module"
        FUNC[2]="build_mk_module"
        
}

all_func()
{
        TITLE="build module  "

        TEXT[1]="build  module"
        FUNC[1]="setup"
            
        TEXT[2]="package module"
        FUNC[2]="package_all"
        
        TEXT[3]="clean module"
        FUNC[3]="clean_all"
}
STEPS[1]="all_func"
STEPS[2]="all_modules_func"
STEPS[3]="all_engine_func"
STEPS[4]="all_media_func"

QUIT=0

while [ "$QUIT" == "0" ]; do
    OPTION_NUM=1
    if [ ! -x "`which wget 2>/dev/null`" ]; then
        echo "Need to install wget."
        break 
    fi
    for s in $(seq ${#STEPS[@]}) ; do
        ${STEPS[s]}

        echo "----------------------------------------------------------"
        echo " Step $s: ${TITLE}"
        echo "----------------------------------------------------------"

        for i in $(seq ${#TEXT[@]}) ; do
            echo "[$OPTION_NUM] ${TEXT[i]}"
            OPTIONS[$OPTION_NUM]=${FUNC[i]}
            let "OPTION_NUM+=1"
        done

        # Clear TEXT and FUNC arrays before next step
        unset TEXT
        unset FUNC

        echo ""
    done

    echo "[$OPTION_NUM] Exit Script"
    OPTIONS[$OPTION_NUM]="quit"
    echo ""
    echo -n "Option: "
    read our_entry
    echo ""
    ${OPTIONS[our_entry]} ${our_entry}
    echo
done
