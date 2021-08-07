# -*- coding:utf-8 -*-

import os
import sys
import codecs
import requests
import json
import datetime
import time

class Builder:
    def __init__(self, upload_dir):
        self.projectPath = os.path.join(os.getcwd())
        self.CompilePath = os.path.join(self.projectPath, 'dii_objc', 'src')
        
        #  rtmp sdk output dir
        self.lib_output_dir =  os.path.join(self.projectPath,'dii_objc', 'src', 'build', 'Release')
        self.remote_dir = "client/dii-media-kit/mac/" + upload_dir
                
    def run(self):
        self.build()
        self.upload_output()

    def build(self):
        if os.path.exists(self.CompilePath):
            os.chdir(self.CompilePath)
            print 'Change dir to ' + self.CompilePath
            ret = -1
            print 'Start build mac dii-media-kit release versioin'
            #clean befor build.
            os.system('xcodebuild clean -project DiiMediaKit.xcodeproj -target DiiMediaKit_Mac')
            #build
            ret = os.system('xcodebuild -project DiiMediaKit.xcodeproj -target DiiMediaKit_Mac') 
            
            if ret != 0 :
                exit(1)
        else:
            print 'error: %s are not exist!' %self.CompilePath

    def upload_output(self):
        os.chdir(self.projectPath)
        version = self.get_sdk_version()
        
        # archive lib
        if os.path.exists(self.lib_output_dir):
            os.chdir(self.lib_output_dir)
            ts = time.strftime("%Y%m%d_%H%M")
            self.targetFile =  os.path.join(self.lib_output_dir, 'dii_mac_' + version + '_' + ts  + '.zip')
            print os.system('zip -r ' + self.targetFile + ' ' + './')
            self.http_login()
            self.http_upload()

        else:
            print '%s path are not exist.' %self.lib_output_dir
    
    def get_sdk_version(self):
        version_file = os.path.join(self.projectPath, 'dii_player', 'dii_common.h')
        version = None
        fd = open(version_file, 'r')
        for line in fd.readlines():
            if 'DII_MEDIA_KIT_VERSION' in line  :
                version = line.split(' ')[-1]
                break
        if version != None:
            return version.strip('\r\n').strip(' ').strip('"')
        else:
            print 'error: can not get version'
            exit(1)
    
    def http_login(self):
        url = "http://sdk.fifo.site/chfs/session"
        params = {"user": (None, "admin"), "pwd": (None, "admin123")}
        res = requests.post(url, files=params)
        if res.status_code != 201:
            print 'file server login failed!'
            exit(1)
        self.Cookie = 'JWT=' + res.cookies['JWT'] + '; ' + 'user=' + res.cookies['user']
        #print self.Cookie
        self.jar = res.cookies
        print 'file server login success!'

    def http_upload(self):
        url = "http://sdk.fifo.site/chfs/upload"
        data = {'file':(self.targetFile, open(self.targetFile, 'rb'), "application/zip"),'folder':(None, self.remote_dir)}
        res = requests.post(url, files=data, cookies=self.jar)
        print res.status_code
        if res.status_code != 201:
            print "file upload failed!"
            exit(1)
        print "upload file success! file: " + self.targetFile

if __name__=='__main__' :
    upload_dir = ''
    if len(sys.argv) == 2:
        upload_dir = sys.argv[1]
    builder = Builder(upload_dir)
    builder.run()
