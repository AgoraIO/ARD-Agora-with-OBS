#include <QMessageBox>
#include <QWindow>
#include <QJsondocument>
#include <QJsonObject>
#include <stdio.h>
#include <obs-properties.h>
#include "window-agora-main.hpp"
#include "../agora-ui-main.h"
#include "obs-module.h"
#include "window-agora-settings.hpp"
#include <util/platform.h>
#include <util/dstr.h>
#include <thread>
#define AGORA_TOOL_VERSION "21.06.21"
#if _WIN32
#else
#include <dispatch/dispatch.h>
#define Sleep(x)          \
    std::this_thread::sleep_for(std::chrono::milliseconds(x))

#endif
using namespace std;

bool DisplayResizeEvent::eventFilter(QObject *obj, QEvent *event)
{
	if (event->type() == QEvent::Resize) {
		emit DisplayResized();
		return true;
	}
	
	return QObject::eventFilter(obj, event);
}

AgoraBasic::AgoraBasic(QMainWindow *parent)
	: QMainWindow(parent, Qt::Dialog)
	, ui(new Ui::AgoraBasic)
	, current_source(nullptr)
	, output(nullptr)
	, audio_encoder(nullptr)
{
	ui->setupUi(this);
	setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint);
	//ui->previewDisabledWidget->setVisible(false);
	ui->menubar->setVisible(false);

	setWindowTitle(QString("Agora RTC Tool ( %1 )").arg(AGORA_TOOL_VERSION));
	blog(LOG_INFO, "agora tool version: Agora RTC Tool ( %s )", AGORA_TOOL_VERSION);
	setAttribute(Qt::WA_QuitOnClose, false);
	curl = curl_easy_init();
	obs_frontend_push_ui_translation(obs_module_get_string);
	control_text  = tr("Agora.Main.Controls");
	start_text    = tr("Agora.Main.Start");
	starting_text = tr("Agora.Main.Starting");
	stop_text     = tr("Agora.Main.Stop");
	stopping_text = tr("Agora.Main.Stopping");
	settings_title = tr("Agora.Settings.DialogTitle");
	empty_appid_info = tr("Agora.General.EmptyAppid");
	empty_channel = tr("Agora.General.EmptyChannel");
	empty_uid = tr("Agora.General.EmptyUID");
	init_failed_info = tr("Agora.General.Init.Failed");

	exit_info = tr("Agora.Main.Exit.Info");
	cpuInformation = tr("Agora.System.CPU.Infomation");
	invalidChannelError = tr("Basic.Main.Agora.Invalid.Channel");
	invalidTokenlError = tr("Basic.Main.Agora.Invalid.Token");
	invalidAppidError = tr("Basic.Main.Agora.Invalid.Appid");
	invalidTokenExpiredError = tr("Basic.Main.Agora.Token.Expired");
	joinFailedInfo = tr("Agora.JoinChannelFailed.Token");
	requertTokenError = tr("Agora.Main.Request.Token.Error");
	obs_frontend_pop_ui_translation();

	ui->controlsDock->setWindowTitle(control_text);
	ui->agoraSteramButton->setText(start_text);
	
	connect(AgoraRtcEngine::GetInstance(), &AgoraRtcEngine::onJoinChannelSuccess, this, &AgoraBasic::onJoinChannelSuccess_slot);
	connect(AgoraRtcEngine::GetInstance(), &AgoraRtcEngine::onLeaveChannel, this, &AgoraBasic::onLeaveChannel_slot);
	connect(AgoraRtcEngine::GetInstance(), &AgoraRtcEngine::onError, this, &AgoraBasic::onError_slot);
	connect(AgoraRtcEngine::GetInstance(), &AgoraRtcEngine::onUserJoined, this, &AgoraBasic::onUserJoined_slot);
	connect(AgoraRtcEngine::GetInstance(), &AgoraRtcEngine::onUserOffline, this, &AgoraBasic::onUserOffline_slot);
	connect(AgoraRtcEngine::GetInstance(), &AgoraRtcEngine::onFirstRemoteVideoDecoded, this, &AgoraBasic::onFirstRemoteVideoDecoded_slot);
	connect(AgoraRtcEngine::GetInstance(), &AgoraRtcEngine::onConnectionStateChanged, this, &AgoraBasic::onConnectionStateChanged_slot);
	connect(AgoraRtcEngine::GetInstance(), &AgoraRtcEngine::onRemoteVideoStateChanged, this, &AgoraBasic::onRemoteVideoStateChanged_slot);
	connect(AgoraRtcEngine::GetInstance(), &AgoraRtcEngine::onFirstRemoteVideoFrame, this, &AgoraBasic::onFirstRemoteVideoFrame_slot);
	connect(&transcodingTimer, &QTimer::timeout, this, &AgoraBasic::transcoding_slot);
	connect(&showRemoteTimer, &QTimer::timeout, this, &AgoraBasic::showRemote_slot);
	connect(AgoraRtcEngine::GetInstance(), &AgoraRtcEngine::onClientRoleChanged, this, &AgoraBasic::onClientRoleChanged_slot);
	connect(&joinFailedTimer, &QTimer::timeout, this, &AgoraBasic::joinFailed_slot);
	connect(this, &AgoraBasic::requestTokenSignal, this, &AgoraBasic::reuquestToken_slot);
	connect(AgoraRtcEngine::GetInstance(), &AgoraRtcEngine::onSystemCPU, this, &AgoraBasic::onSystemCPU_slot);

	CreateRemoteVideos();
	auto addDrawCallback = [this]() {
		obs_display_add_draw_callback(display,
			AgoraBasic::DrawPreview, this);
	};
	connect(this, &AgoraBasic::DisplayCreated,addDrawCallback);
	auto displayResize = [this]() {
		struct obs_video_info ovi;

		if (obs_get_video_info(&ovi)) {
			QSize size = ui->preview->size() * ui->preview->devicePixelRatioF();
			obs_display_resize(display, size.width(), size.height());
		}
	};

	connect(&resizeEventHandler, &DisplayResizeEvent::DisplayResized, displayResize);
	ui->preview->setAttribute(Qt::WA_PaintOnScreen);
	ui->preview->setAttribute(Qt::WA_StaticContents);
	ui->preview->setAttribute(Qt::WA_NoSystemBackground);
	ui->preview->setAttribute(Qt::WA_OpaquePaintEvent);
	ui->preview->setAttribute(Qt::WA_DontCreateNativeAncestors);
	ui->preview->setAttribute(Qt::WA_NativeWindow);
	ui->preview->setUpdatesEnabled(false);
	ui->preview->installEventFilter(&resizeEventHandler);

#if WIN32
	CreateDisplay();
#endif

	InitializeAgoraOutput();
	
	QMainWindow* mainWindow = (QMainWindow*)obs_frontend_get_main_window();

	char path[512];
	int len = os_get_config_path(path, sizeof(path), "obs-studio\\logs\\");
	AgoraRtcEngine::GetInstance()->pcmFolder = path;
	InitGlobalConfig();
	InitBasicConfig();

	obs_frontend_add_event_callback(AgoraBasic::OBSEvent, this);
}

void AgoraBasic::InitGlobalConfig()
{
	char path[512];

	int len = os_get_config_path(path, sizeof(path), "obs-studio/global.ini");
	if (len <= 0)
		return;
	int errorcode = globalConfig.Open(path, CONFIG_OPEN_ALWAYS);
	if (errorcode != CONFIG_SUCCESS) {
		blog(LOG_ERROR, "Agora Plugin Failed to open global.ini: %d", errorcode);
		return ;
	}

	char pathAgora[512];

	int lenAgora = os_get_config_path(pathAgora, sizeof(pathAgora), "obs-studio/globalAgora.ini");
	if (lenAgora <= 0)
		return;
	errorcode = globalAgoraConfig.Open(pathAgora, CONFIG_OPEN_ALWAYS);
	if (errorcode != CONFIG_SUCCESS) {
		blog(LOG_ERROR, "Agora Plugin Failed to open globalAgora.ini: %d", errorcode);
		return;
	}
}

int AgoraBasic::GetProfilePath(char *path, size_t size, const char *file)
{
	char profiles_path[512];
	const char *profile =
		config_get_string(globalConfig, "Basic", "ProfileDir");
	int ret;

	if (!profile)
		return -1;
	if (!path)
		return -1;
	if (!file)
		file = "";

	ret = os_get_config_path(profiles_path, 512, "obs-studio/basic/profiles");
	if (ret <= 0)
		return ret;

	if (!*file)
		return snprintf(path, size, "%s/%s", profiles_path, profile);

	return snprintf(path, size, "%s/%s/%s", profiles_path, profile, file);
}

void AgoraBasic::ResetBasicConfig()
{
	basicConfig.Close();
	char configPath[512];

	int ret = GetProfilePath(configPath, sizeof(configPath), "");
	if (ret <= 0) {
		blog(LOG_INFO, "Failed to get profile path");
		return;
	}

	ret = GetProfilePath(configPath, sizeof(configPath), "basic.ini");
	if (ret <= 0) {
		blog(LOG_INFO, "Failed to get basic.ini path");
		return;
	}

	int code = basicConfig.Open(configPath, CONFIG_OPEN_ALWAYS);
	if (code != CONFIG_SUCCESS) {
		blog(LOG_INFO, "Failed to open basic.ini: %d", code);
		return;
	}
}

void AgoraBasic::InitBasicConfig()
{
	char configPath[512];

	int ret = GetProfilePath(configPath, sizeof(configPath), "");
	if (ret <= 0) {
		blog(LOG_INFO, "Failed to get profile path");
		return ;
	}

	ret = GetProfilePath(configPath, sizeof(configPath), "basic.ini");
	if (ret <= 0) {
		blog(LOG_INFO, "Failed to get basic.ini path");
		return ;
	}

	int code = basicConfig.Open(configPath, CONFIG_OPEN_ALWAYS);
	if (code != CONFIG_SUCCESS) {
		blog(LOG_INFO, "Failed to open basic.ini: %d", code);
		return;
	}

	if (config_has_user_value(basicConfig, "General", "Name")
		&& config_get_string(basicConfig, "General", "Name") == nullptr) {
		if (config_has_user_value(globalConfig, "Basic", "Profile"))
			const char *curName = config_get_string(globalConfig,
				"Basic", "Profile");
	}

	m_agoraToolSettings.savePersist = config_get_bool(globalAgoraConfig, "AgoraTool", "savePersist");

	if (m_agoraToolSettings.savePersist) {
		if (config_has_user_value(globalAgoraConfig, "AgoraTool", "token"))
			m_agoraToolSettings.token = config_get_string(globalAgoraConfig, "AgoraTool", "token");
		if (config_has_user_value(globalAgoraConfig, "AgoraTool", "rtmp_url"))
			m_agoraToolSettings.rtmp_url = config_get_string(globalAgoraConfig, "AgoraTool", "rtmp_url");
		if (config_has_user_value(globalAgoraConfig, "AgoraTool", "channelName"))
			m_agoraToolSettings.channelName = config_get_string(globalAgoraConfig, "AgoraTool", "channelName");
		m_agoraToolSettings.uid = config_get_uint(globalAgoraConfig, "AgoraTool", "uid");

		m_agoraToolSettings.agora_fps = config_get_int(globalAgoraConfig, "AgoraTool", "agora_fps");
		m_agoraToolSettings.agora_bitrate = config_get_int(globalAgoraConfig, "AgoraTool", "agora_bitrate");
		m_agoraToolSettings.agora_width = config_get_int(globalAgoraConfig, "AgoraTool", "agora_width");
		m_agoraToolSettings.agora_height = config_get_int(globalAgoraConfig, "AgoraTool", "agora_height");


		m_agoraToolSettings.rtmp_fps = config_get_int(globalAgoraConfig, "AgoraTool", "rtmp_fps");
		m_agoraToolSettings.rtmp_bitrate = config_get_int(globalAgoraConfig, "AgoraTool", "rtmp_bitrate");
		m_agoraToolSettings.rtmp_width = config_get_int(globalAgoraConfig, "AgoraTool", "rtmp_width");
		m_agoraToolSettings.rtmp_height = config_get_int(globalAgoraConfig, "AgoraTool", "rtmp_height");

		m_agoraToolSettings.audioChannel = config_get_int(globalAgoraConfig, "AgoraTool", "audioChannel");
		m_agoraToolSettings.scenario = config_get_int(globalAgoraConfig, "AgoraTool", "scenario");
		m_agoraToolSettings.obs_bitrate = config_get_int(globalAgoraConfig, "AgoraTool", "obs_bitrate");
		m_agoraToolSettings.videoEncoder = config_get_int(globalAgoraConfig, "AgoraTool", "videoEncoder");
		m_agoraToolSettings.muteAllRemoteAudioVideo = config_get_bool(globalAgoraConfig, "AgoraTool", "muteAllRemoteAudioVideo");
		m_agoraToolSettings.bHighQuality = config_get_bool(globalAgoraConfig, "AgoraTool", "bHighQuality");
		if (config_has_user_value(globalAgoraConfig, "AgoraTool", "InformationMode"))
			m_agoraToolSettings.info_mode = config_get_int(globalAgoraConfig, "AgoraTool", "InformationMode");
		if(config_has_user_value(globalAgoraConfig, "AgoraTool", "InformationUrl"))
		   m_agoraToolSettings.information_url = config_get_string(globalAgoraConfig, "AgoraTool", "InformationUrl");

		if (config_has_user_value(globalAgoraConfig, "AgoraTool", "CPUThreshold"))
			m_agoraToolSettings.cpuThreshold = config_get_int(globalAgoraConfig, "AgoraTool", "CPUThreshold");
	}

	m_agoraToolSettings.savePersistAppid = config_get_bool(globalAgoraConfig, "AgoraTool", "savePersistAppid");
	if(m_agoraToolSettings.savePersistAppid	&& m_agoraToolSettings.info_mode == 0)
		m_agoraToolSettings.appid = config_get_string(globalAgoraConfig, "AgoraTool", "appid");
}

AgoraBasic::~AgoraBasic()
{
	if (current_source)
		obs_source_release(current_source);

	if (output)
		obs_output_release(output);

	if (audio_encoder)
		obs_encoder_release(audio_encoder);

	if (curl) {
		curl_easy_cleanup(curl);
	}

	AgoraRtcEngine::GetInstance()->ReleaseInstance();

	if (m_agoraToolSettings.savePersist) {
		if (m_agoraToolSettings.info_mode == 0) {//manually
			config_set_string(globalAgoraConfig, "AgoraTool", "appid", m_agoraToolSettings.appid.c_str());
			config_set_string(globalAgoraConfig, "AgoraTool", "token", m_agoraToolSettings.token.c_str());
			config_set_string(globalAgoraConfig, "AgoraTool", "channelName", m_agoraToolSettings.channelName.c_str());
			config_set_uint(globalAgoraConfig, "AgoraTool", "uid", m_agoraToolSettings.uid);
		}
		else {
			config_set_uint(globalAgoraConfig, "AgoraTool", "uid", 0);
			config_set_string(globalAgoraConfig, "AgoraTool", "appid", "");
			config_set_string(globalAgoraConfig, "AgoraTool", "token", "");
			config_set_string(globalAgoraConfig, "AgoraTool", "channelName", "");
		}
		
		config_set_uint(globalAgoraConfig, "AgoraTool", "InformationMode", m_agoraToolSettings.info_mode);

		config_set_string(globalAgoraConfig, "AgoraTool", "rtmp_url", m_agoraToolSettings.rtmp_url.c_str());
		config_set_int(globalAgoraConfig, "AgoraTool", "agora_fps", m_agoraToolSettings.agora_fps);
		config_set_int(globalAgoraConfig, "AgoraTool", "agora_bitrate", m_agoraToolSettings.agora_bitrate);
		config_set_int(globalAgoraConfig, "AgoraTool", "agora_width", m_agoraToolSettings.agora_width);
		config_set_int(globalAgoraConfig, "AgoraTool", "agora_height", m_agoraToolSettings.agora_height);

		config_set_int(globalAgoraConfig, "AgoraTool", "rtmp_fps", m_agoraToolSettings.rtmp_fps);
		config_set_int(globalAgoraConfig, "AgoraTool", "rtmp_bitrate", m_agoraToolSettings.rtmp_bitrate);
		config_set_int(globalAgoraConfig, "AgoraTool", "rtmp_width", m_agoraToolSettings.rtmp_width);
		config_set_int(globalAgoraConfig, "AgoraTool", "rtmp_height", m_agoraToolSettings.rtmp_height);

		config_set_int(globalAgoraConfig, "AgoraTool", "audioChannel", m_agoraToolSettings.audioChannel);
		config_set_int(globalAgoraConfig, "AgoraTool", "scenario", m_agoraToolSettings.scenario);
		config_set_int(globalAgoraConfig, "AgoraTool", "obs_bitrate", m_agoraToolSettings.obs_bitrate);
		config_set_int(globalAgoraConfig, "AgoraTool", "videoEncoder", m_agoraToolSettings.videoEncoder);

		config_set_int(globalAgoraConfig, "AgoraTool", "CPUThreshold", m_agoraToolSettings.cpuThreshold);

		config_set_bool(globalAgoraConfig, "AgoraTool", "muteAllRemoteAudioVideo", m_agoraToolSettings.muteAllRemoteAudioVideo);
		config_set_bool(globalAgoraConfig, "AgoraTool", "bHighQuality", m_agoraToolSettings.bHighQuality);
		config_set_bool(globalAgoraConfig, "AgoraTool", "savePersist", m_agoraToolSettings.savePersist);
		config_set_string(globalAgoraConfig, "AgoraTool", "InformationUrl", m_agoraToolSettings.information_url.c_str());
	}
	else {
		config_set_uint(globalAgoraConfig, "AgoraTool", "InformationMode", 0);
		config_set_string(globalAgoraConfig, "AgoraTool", "InformationUrl", "");
		config_set_string(globalAgoraConfig, "AgoraTool", "appid", "");
		config_set_string(globalAgoraConfig, "AgoraTool", "token", "");
		config_set_string(globalAgoraConfig, "AgoraTool", "rtmp_url", "");
		config_set_string(globalAgoraConfig, "AgoraTool", "channelName", "");
		config_set_uint(globalAgoraConfig, "AgoraTool", "uid", 0);

		config_set_int(globalAgoraConfig, "AgoraTool", "agora_fps", 0);
		config_set_int(globalAgoraConfig, "AgoraTool", "agora_bitrate", 0);
		config_set_int(globalAgoraConfig, "AgoraTool", "agora_width", 0);
		config_set_int(globalAgoraConfig, "AgoraTool", "agora_height", 0);

		config_set_int(globalAgoraConfig, "AgoraTool", "rtmp_fps", 0);
		config_set_int(globalAgoraConfig, "AgoraTool", "rtmp_bitrate", 0);
		config_set_int(globalAgoraConfig, "AgoraTool", "rtmp_width", 0);
		config_set_int(globalAgoraConfig, "AgoraTool", "rtmp_height", 0);

		config_set_int(globalAgoraConfig, "AgoraTool", "audioChannel", 0);
		config_set_int(globalAgoraConfig, "AgoraTool", "scenario", 0);
		config_set_int(globalAgoraConfig, "AgoraTool", "obs_bitrate", 0);
		config_set_int(globalAgoraConfig, "AgoraTool", "videoEncoder", 0);
		config_set_int(globalAgoraConfig, "AgoraTool", "CPUThreshold", 0);

		config_set_bool(globalAgoraConfig, "AgoraTool", "muteAllRemoteAudioVideo", false);
		config_set_bool(globalAgoraConfig, "AgoraTool", "bHighQuality", false);
		config_set_bool(globalAgoraConfig, "AgoraTool", "savePersist", false);
	}
	
	if(m_agoraToolSettings.savePersistAppid && m_agoraToolSettings.info_mode == 0)//manually
		config_set_string(globalAgoraConfig, "AgoraTool", "appid", m_agoraToolSettings.appid.c_str());
	else
		config_set_string(globalAgoraConfig, "AgoraTool", "appid", "");
	config_set_bool(globalAgoraConfig, "AgoraTool", "savePersistAppid", m_agoraToolSettings.savePersistAppid);

	globalAgoraConfig.Save();
}

int AgoraBasic::GetOBSBitrate()
{
	if (!basicConfig)
		return 2500;
	const char *mode = config_get_string(basicConfig, "Output", "Mode");
	bool advOut = astrcmpi(mode, "Advanced") == 0;

	int bitrate = 2500;
	if (advOut) {
		bitrate = config_get_int(basicConfig, "AdvOut", "VBitrate");
	}
	else {
		bitrate = config_get_int(basicConfig, "SimpleOutput", "VBitrate");
	}

	return bitrate == 0 ? 2500 : bitrate;
}

size_t http_callback(void *str, size_t size, size_t count, void *out_str)
{
	if (NULL != out_str)
	{
		std::string &s_res = *(std::string *)out_str;
		s_res.append((const char *)str, size * count);
	}
	return size * count;
}

void AgoraBasic::on_agoraSteramButton_clicked()
{
	QString str = ui->agoraSteramButton->text();
  
	if (!joinFailed && (starting_text.compare(str) == 0 ||
		stopping_text.compare(str) == 0)) {
		return;
	}

	if (start_text.compare(str) == 0) {		
		//http url
		if (m_agoraToolSettings.info_mode == 1) {
			ui->agoraSteramButton->setText(starting_text);
			std::thread th([this](){

				//connect timeout
				curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5); 
				// get data timeout
				curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);

				//cancel verify
				curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
				curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);
				curl_slist* header_list = NULL;
				header_list = curl_slist_append(header_list, "Content-Type:application/json;charset=UTF-8");
				/*header_list = curl_slist_append(header_list, auth_info.c_str());*/
				curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);

				//callback
				std::string json_res;
				json_res.clear();
				curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_callback);
				curl_easy_setopt(curl, CURLOPT_WRITEDATA, &json_res);

				//set URL
				
				//set POST data 
				//char szUrl[1024] = { 0 };
				//sprintf_s(szUrl, 1024, "%s?appid=%s&&uid=%u&channelName=%s", m_agoraToolSettings.information_url.c_str()
				//	,m_agoraToolSettings.appid.c_str(), m_agoraToolSettings.uid, m_agoraToolSettings.channelName.c_str());

				curl_easy_setopt(curl, CURLOPT_URL, m_agoraToolSettings.information_url.c_str());
				blog(LOG_INFO, "agora token url:%s", m_agoraToolSettings.information_url.c_str());
				//curl_easy_setopt(curl, CURLOPT_POSTFIELDS, szPostData);
				//blog(LOG_INFO, "agora tokenurl:%s, post data=%s", m_agoraToolSettings.information_url.c_str(), szPostData);

				int res = curl_easy_perform(curl);
				blog(LOG_INFO, "agora request url res:%d", res);
				if (nullptr != header_list){
					curl_slist_free_all(header_list);
				}

				if (res != CURLE_OK) {
					emit requestTokenSignal("", -1); 
					//QMessageBox::information(NULL, QString(""), requertTokenError);
					return;
				}
				blog(LOG_INFO, "request token result is %s.", json_res.c_str());
				emit this->requestTokenSignal(QString::fromUtf8(json_res.c_str()), 0);
			});
			th.join();
		}
		else 
		{
			joinChannel(m_agoraToolSettings.token);
		}		
	}
	else {
		obs_remove_raw_video_callback(RawVideoCallback, this);
		StopAgoraOutput();
		AgoraRtcEngine::GetInstance()->stopPreview();

		//ResetEvent(stopSignal);
		if (joinFailed)
			AgoraRtcEngine::GetInstance()->SetJoinChannel(true);
		AgoraRtcEngine::GetInstance()->leaveChannel();
		//SetEvent(stopSignal);
		
		if (!m_agoraToolSettings.agora_url.empty())
			AgoraRtcEngine::GetInstance()->RemovePublishStreamUrl(m_agoraToolSettings.agora_url.c_str());
		if (starting_text.compare(str) != 0) {
			ui->agoraSteramButton->setText(stopping_text);
		}
		
		ClearRemoteVideos();
		m_lstRemoteVideoUids.clear();
		m_lstUids.clear();
	}
}

void AgoraBasic::joinChannel(std::string token)
{
	if (m_agoraToolSettings.appid.empty()) {
		QMessageBox::about(nullptr, settings_title, empty_appid_info);
		return;
	}
	if (m_agoraToolSettings.channelName.empty()) {
		QMessageBox::about(nullptr, settings_title, empty_channel);
		return;
	}

	if (!m_agoraToolSettings.appCerf.empty() &&
		m_agoraToolSettings.uid == 0) {
		QMessageBox::information(NULL, QString(""), empty_uid);
		return;
	}
	if (!AgoraRtcEngine::GetInstance()->IsInitialize()) {
		if (!AgoraRtcEngine::GetInstance()->InitEngine(m_agoraToolSettings.appid)) {
			QMessageBox::information(NULL, QString(""), init_failed_info);
			return;
		}
	}

	if (current_source) {
		obs_get_video_info(&ovi);
		video_scale_info info;
		info.width = ovi.output_width;
		info.height = ovi.output_height;
		info.format = ovi.output_format;
		info.range = ovi.range;
		info.colorspace = ovi.colorspace;
		obs_add_raw_video_callback(&info, RawVideoCallback, (void*)this);
	}

	StartAgoraOutput();

	int output_width = ovi.output_width;
	int output_height = ovi.output_height;

	if (output_width*output_height > 1920 * 1080) {
		float rate = sqrtf((float)(output_width*output_height) / (1920 * 1080.0f));
		float width = (float)output_width / rate;
		float height = (float)output_height / rate;
		output_width = width;
		output_height = height;
	}

	//dump pcm
	AgoraRtcEngine::GetInstance()->SavePcm(m_agoraToolSettings.SavePCM);

	if (m_agoraToolSettings.videoEncoder == 0) {//Agora 
		AgoraRtcEngine::GetInstance()->setVideoProfileEx(
			output_width,
			output_height,
			(float)ovi.fps_num / (float)ovi.fps_den,
			m_agoraToolSettings.agora_bitrate, true);
	}
	else {//obs 
		m_agoraToolSettings.obs_bitrate = GetOBSBitrate();
		AgoraRtcEngine::GetInstance()->setVideoProfileEx(
			output_width,
			output_height,
			(float)ovi.fps_num / (float)ovi.fps_den,
			m_agoraToolSettings.obs_bitrate);
	}
	joinFailedTimer.stop();
	joinFailedTimer.start(10000);
	blog(LOG_INFO, "agora token:%s", m_agoraToolSettings.token.c_str());
	AgoraRtcEngine::GetInstance()->joinChannel(m_agoraToolSettings.token.c_str()
		, m_agoraToolSettings.channelName.c_str(), m_agoraToolSettings.uid,
		!m_agoraToolSettings.muteAllRemoteAudioVideo, !m_agoraToolSettings.muteAllRemoteAudioVideo);
	ui->agoraSteramButton->setText(starting_text);
}

void AgoraBasic::reuquestToken_slot(QString json, int err)
{
	if (err == 0) {
		blog(LOG_INFO, "agora request token: %s", json.toStdString().c_str());

		QJsonDocument doc = QJsonDocument::fromJson(json.toStdString().c_str());
		if (doc.isNull() || !doc.isObject()) {
			QMessageBox::information(NULL, QString(""), requertTokenError);
			blog(LOG_INFO, "request token result is null or not json");
			ui->agoraSteramButton->setText(start_text);
			return;
		}

		QJsonObject jsonObject = doc.object();

		int code = jsonObject["code"].toInt();
		if (code != 0) {
			QMessageBox::information(NULL, QString(""), requertTokenError);
			ui->agoraSteramButton->setText(start_text);
			return;
		}

		QJsonObject jsData = jsonObject["data"].toObject();
		if (doc.isNull() || !doc.isObject()) {
			blog(LOG_INFO, "http request error");
			ui->agoraSteramButton->setText(start_text);
			return;
		}
#if _WIN32
    m_agoraToolSettings.appid = jsData["appID"].toString().toUtf8();
    m_agoraToolSettings.channelName = jsData["channelName"].toString().toUtf8();
    m_agoraToolSettings.token = jsData["token"].toString().toUtf8();
#else
    m_agoraToolSettings.appid = jsData["appID"].toString().toStdString();
    m_agoraToolSettings.channelName = jsData["channelName"].toString().toStdString();
    m_agoraToolSettings.token = jsData["token"].toString().toStdString();
#endif

		m_agoraToolSettings.uid = strtoul(jsData["uid"].toString().toUtf8().data(), nullptr, 10);
		joinChannel(m_agoraToolSettings.token);

		return;
	}
	else if(err == -1){
		QMessageBox::information(NULL, QString(""), requertTokenError);
	}


	ui->agoraSteramButton->setText(start_text);
}

void AgoraBasic::showEvent(QShowEvent *event)
{
	if (!current_source)
		current_source = obs_frontend_get_current_scene();

	QMainWindow::showEvent(event);
}

void AgoraBasic::hideEvent(QHideEvent *event)
{
	QMainWindow::hideEvent(event);
}

void AgoraBasic::closeEvent(QCloseEvent *event)
{
	QString str = ui->agoraSteramButton->text();
	if (stop_text.compare(str) == 0) {
		on_agoraSteramButton_clicked();
		
	}
	
	QMainWindow::closeEvent(event);
}

void AgoraBasic::ToggleAgoraDialog() {
	if (!isVisible()) {
		
		ResetBasicConfig();
		m_agoraToolSettings.obs_bitrate = GetOBSBitrate();
		setVisible(true);
		blog(LOG_INFO, "show dialog");
	}
	else {
		setVisible(false);
		blog(LOG_INFO, "hide dialog");
	}
}

void AgoraBasic::on_settingsButton_clicked()
{
	AgoraSettings settings(this);
	settings.setWindowIconText(settings_title);
	int ret= settings.exec();

	if (ret == QDialog::Accepted) {
		if (AgoraRtcEngine::GetInstance()->IsJoinChannel()) {
			AgoraRtcEngine::GetInstance()->MuteAllRemoteAudio(m_agoraToolSettings.muteAllRemoteAudioVideo);
			AgoraRtcEngine::GetInstance()->MuteAllRemoteVideo(m_agoraToolSettings.muteAllRemoteAudioVideo);

			if (m_lstRemoteVideoUids.size() > 0 && m_agoraToolSettings.muteAllRemoteAudioVideo) {
				ClearRemoteVideos();
				m_lstRemoteVideoUids.clear();
				if (!m_agoraToolSettings.rtmp_url.empty())
					SetLiveTranscoding();
			}
		}
	}
}

void AgoraBasic::on_exitButton_clicked()
{
	QString str = ui->agoraSteramButton->text();
	if (stop_text.compare(str) != 0) {
		return;
	}
}

void AgoraBasic::SetLiveTranscoding()
{
	if (m_agoraToolSettings.rtmp_url.empty())
		return;
	int count = m_lstRemoteVideoUids.size();
	count = count > REMOTE_VIDEO_COUNT ? REMOTE_VIDEO_COUNT : count;

	LiveTranscoding config;
	config.audioSampleRate = AUDIO_SAMPLE_RATE_48000;
	config.audioChannels = 2;
	config.width = m_agoraToolSettings.rtmp_width;   //;
	config.height = m_agoraToolSettings.rtmp_height; //;
	config.videoFramerate = m_agoraToolSettings.rtmp_fps;
	config.videoBitrate = m_agoraToolSettings.rtmp_height;
	config.videoGop = config.videoFramerate;
	config.userCount = count + 1;
	config.watermark = nullptr;
	config.videoCodecProfile = VIDEO_CODEC_PROFILE_MAIN;
	config.transcodingUsers = new TranscodingUser[config.userCount];
	config.lowLatency = true;

	int row = 1;
	int col = 1;

	
	if (count == 2) {
		row = 2;
		col = 1;
	}
	else if (count > 2 && count <= 4) {
		row = col = 2;
	}
	else if (count > 5 && count <= 6) {
		row = 2;
		col = 3;
	}
	else if (count > 6 && count <= 9) {
		row = 3;
		col = 3;
	}
	else if (count > 9 && count <= 12) {
		row = 3;
		col = 4;
	}
	else if (count > 12 && count <= 16) {
		row = 4;
		col = 4;
	}

	if (count == 0) {
		config.transcodingUsers[0].x = 0;
		config.transcodingUsers[0].y = 0;
		config.transcodingUsers[0].width = m_agoraToolSettings.rtmp_width;
		config.transcodingUsers[0].height = m_agoraToolSettings.rtmp_height;
		config.transcodingUsers[0].zOrder = 1;
		config.transcodingUsers[0].uid = local_uid;
		
	}
	else {
		int width = m_agoraToolSettings.rtmp_width / 2;
		int height = m_agoraToolSettings.rtmp_height;

		config.transcodingUsers[0].x = 0;
		config.transcodingUsers[0].y = 0;
		config.transcodingUsers[0].width = width;
		config.transcodingUsers[0].height = height;
		config.transcodingUsers[0].zOrder = 1;
		config.transcodingUsers[0].uid = local_uid;
		
		int w = width / col;
		int h = height / row;

		int index = 0;
		for (int i = 0; i < row - 1; ++i) {
			for (int j = 0; j < col; ++j) {
				index = i * col + j + 1;
				if (index > count)
					break;
				config.transcodingUsers[index].x = width + j * w;
				config.transcodingUsers[index].y = i * h;
				config.transcodingUsers[index].width = w;
				config.transcodingUsers[index].height = h;
				config.transcodingUsers[index].zOrder = 1;
				config.transcodingUsers[index].uid = uids[index];
		
			}
		}

		int rest = count - (row - 1)*col;
		w = width / rest;
		for (int j = 0; j < rest; ++j) {
			index = (row - 1) * col + j + 1;
			if (index > count)
				break;
			config.transcodingUsers[index].x = width + j * w;
			config.transcodingUsers[index].y = (row - 1) * h;
			config.transcodingUsers[index].width = w;
			config.transcodingUsers[index].height = h;
			config.transcodingUsers[index].zOrder = 1;
			config.transcodingUsers[index].uid = uids[index];
		}
	}

	blog(LOG_INFO, "SetLiveTranscoding begin count:%d", config.userCount);
	for (int i = 0; i < config.userCount; ++i) {
		blog(LOG_INFO, "uid:%u, index:%d, x:%d, y:%d, w:%d, h:%d", config.transcodingUsers[i].uid, i, config.transcodingUsers[i].x, config.transcodingUsers[i].y, config.transcodingUsers[i].width, config.transcodingUsers[i].height);
	}
	blog(LOG_INFO, "SetLiveTranscoding end");
	AgoraRtcEngine::GetInstance()->SetLiveTranscoding(config);

	delete[] config.transcodingUsers;
	config.transcodingUsers = NULL;
}

void AgoraBasic::CreateRemoteVideos()
{
	for (int i = 0; i < REMOTE_VIDEO_COUNT; i++) {
		remoteVideoInfos[i].remoteVideo = new QWidget;
		//remoteVideos[i] = new QWidget;
		remoteVideoInfos[i].remoteVideo->setSizePolicy(
			QSizePolicy::Expanding, QSizePolicy::Expanding);
		remoteVideoInfos[i].uid = 0;
		remoteVideoInfos[i].iRemoteVideoHLayout = -1;
		remoteVideoInfos[i].remoteVideo->setUpdatesEnabled(true);
	}

	remoteVideoLayout = new QVBoxLayout();
	for (int i = 0; i < REMOTE_VIDEO_ROW; i++) {
		remoteVideoHLayout[i] = new QHBoxLayout();
		remoteVideoLayout->addLayout(remoteVideoHLayout[i]);
	}

	ui->previewLayout->addLayout(remoteVideoLayout);
}

void AgoraBasic::DestroyRemoteVideos()
{
	for (int row = 0; row < REMOTE_VIDEO_ROW; row++) {
		for (int col = 0; col < REMOTE_VIDEO_COL; col++) {
			int i = row * REMOTE_VIDEO_COL + col;
			if (remoteVideoInfos[i].remoteVideo) {
				remoteVideoHLayout[row]->removeWidget(
					remoteVideoInfos[i].remoteVideo);
			}
		}
	}

	for (int i = 0; i < REMOTE_VIDEO_ROW; i++) {
		delete remoteVideoHLayout[i];
		remoteVideoHLayout[i] = nullptr;
	}

	delete remoteVideoLayout;
	remoteVideoLayout = nullptr;
}

void AgoraBasic::ClearRemoteVideos()
{
	blog(LOG_INFO, "ClearRemoteVideos");
	for (int i = 0; i < REMOTE_VIDEO_COUNT; ++i) {
		int index = remoteVideoInfos[i].iRemoteVideoHLayout;
		if (index >= 0 && remoteVideoInfos[i].uid > 0) {
			ResetRemoteVideoWidget(i);
		}
	}


	blog(LOG_INFO, "remote widget info:");
	for (int i = 0; i < m_lstRemoteVideoUids.size(); ++i) {
		int index = remoteVideoInfos[i].iRemoteVideoHLayout;
	}
}

void AgoraBasic::ResetRemoteVideoWidget(int index)
{
	remoteVideoHLayout[remoteVideoInfos[index].iRemoteVideoHLayout]->removeWidget(remoteVideoInfos[index].remoteVideo);
	AgoraRtcEngine::GetInstance()->setupRemoteVideo(remoteVideoInfos[index].uid, nullptr);
	delete remoteVideoInfos[index].remoteVideo;
	remoteVideoInfos[index].remoteVideo = new QWidget;
	remoteVideoInfos[index].remoteVideo->setSizePolicy(
		QSizePolicy::Expanding, QSizePolicy::Expanding);
	remoteVideoInfos[index].iRemoteVideoHLayout = -1;
	remoteVideoInfos[index].uid = 0;
	remoteVideoInfos[index].remoteVideo->setUpdatesEnabled(true);
}

void AgoraBasic::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
#if _WIN32
#else
  dispatch_async(dispatch_get_main_queue(), ^{
    CreateDisplay();
  });
#endif
  if (isVisible() && display) {
#if _WIN32
    QSize size = ui->preview->size();
#else
    QSize size = ui->preview->size() *  ui->preview->devicePixelRatioF();
#endif
    obs_display_resize(display, size.width(), size.height());

  }
}

void AgoraBasic::paintEvent(QPaintEvent *event)
{
	QWidget::paintEvent(event);
}

void AgoraBasic::CreateDisplay()
{
	QWindow* window = ui->preview->windowHandle();
	if (display)
		return;
#if _WIN32
	QSize size = this->size();
#else
  QSize size = this->size() *  ui->preview->devicePixelRatioF();
#endif
	gs_init_data info = {};
	info.cx = size.width();
	info.cy = size.height();
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;
	
#ifdef _WIN32
	info.window.hwnd = (HWND)ui->preview->winId();
#elif __APPLE__
	info.window.view = (id)ui->preview->winId();
#else
	info.window.id = windowId;
	info.window.display = QX11Info::display();
#endif
	display = obs_display_create(&info, backgroundColor);

	emit DisplayCreated(this);
}

static inline void GetScaleAndCenterPos(int baseCX, int baseCY, int windowCX,
	int windowCY, int &x, int &y,
	float &scale)
{
	double windowAspect, baseAspect;
	int newCX, newCY;

	windowAspect = double(windowCX) / double(windowCY);
	baseAspect = double(baseCX) / double(baseCY);

	if (windowAspect > baseAspect) {
		scale = float(windowCY) / float(baseCY);
		newCX = int(double(windowCY) * baseAspect);
		newCY = windowCY;
	}
	else {
		scale = float(windowCX) / float(baseCX);
		newCX = windowCX;
		newCY = int(float(windowCX) / baseAspect);
	}

	x = windowCX / 2 - newCX / 2;
	y = windowCY / 2 - newCY / 2;
}

void AgoraBasic::DrawPreview(void *data, uint32_t cx, uint32_t cy)
{
	AgoraBasic *window = static_cast<AgoraBasic *>(data);

	if (!window->current_source)
		return;

	uint32_t sourceCX = max(obs_source_get_width(window->current_source), 1u);
	uint32_t sourceCY = max(obs_source_get_height(window->current_source), 1u);

	int x = 0, y = 0;
	int newCX, newCY;
	float scale;

	GetScaleAndCenterPos(sourceCX, sourceCY, cx, cy, x, y, scale);

	newCX = int(scale * float(sourceCX));
	newCY = int(scale * float(sourceCY));

	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY), -100.0f, 100.0f);
	gs_set_viewport(x, y, newCX, newCY);
	obs_render_main_texture_src_color_only();
	gs_projection_pop();
	gs_viewport_pop();
}

void AgoraBasic::RawVideoCallback (void *param, struct video_data *frame)
{
	struct obs_video_info ovi;
	AgoraBasic* basic = (AgoraBasic*)param;
	
	if (obs_get_video_info(&ovi) && basic->started) {
		AgoraRtcEngine::GetInstance()->PushVideoFrame(frame);
	}
}

bool AgoraBasic::InitializeAgoraOutput()
{
	audio_encoder = obs_audio_encoder_create("agora_pcm", "agora_raw_audio", nullptr, 0, nullptr);
	obs_encoder_set_audio(audio_encoder, obs_get_audio());

	obs_data_t *audio_settings = obs_data_create();

	struct obs_audio_info ai;
	obs_get_audio_info(&ai);
	uint32_t sampleRate = ai.samples_per_sec;
	int channelSetup = ai.speakers;
	obs_data_set_int(audio_settings, "SampleRate", 44100);
	obs_data_set_int(audio_settings, "ChannelSetup", 2);
	obs_encoder_update(audio_encoder, audio_settings);

	output = obs_output_create("agora_output", "agora_output",
		nullptr, NULL);

	obs_output_set_audio_encoder(output, audio_encoder, 0);
	if (!obs_output_initialize_encoders(output, 0)) {
		blog(LOG_ERROR, "initialize agora output encoder failed");
		return false;
	}
	blog(LOG_INFO, "initialize agora output encoder success");
	obs_data_release(audio_settings);
	return true;
}

bool AgoraBasic::StartAgoraOutput()
{
	if (started)
		return true;

	started = obs_output_start(output);
	if (!started) {
		obs_output_stop(output);
		blog(LOG_ERROR, "start agora_output failed");
		return false;
	}
	blog(LOG_INFO, "start agora_output success");
	return true;
}

void AgoraBasic::StopAgoraOutput()
{
	if (!output || !started)
		return;
	obs_output_force_stop(output);
	started = false;
}

void AgoraBasic::onJoinChannelSuccess_slot(const char* channel, unsigned int uid, int elapsed)
{
	joinFailedTimer.stop();
	uids[0] = uid;
	local_uid = uid;
	ui->agoraSteramButton->setText(stop_text);
	ui->exitButton->setEnabled(false);
	m_agoraToolSettings.uid = uid;
	AgoraRtcEngine::GetInstance()->SetJoinChannel(true);
	if (!m_agoraToolSettings.rtmp_url.empty()) {
		if (m_agoraToolSettings.rtmp_width == 0
			|| m_agoraToolSettings.rtmp_height == 0
			|| m_agoraToolSettings.rtmp_bitrate == 0
			|| m_agoraToolSettings.rtmp_fps == 0) {
			return;
		}
		SetLiveTranscoding();
		AgoraRtcEngine::GetInstance()->AddPublishStreamUrl(m_agoraToolSettings.rtmp_url.c_str(), true);
	}
}

void AgoraBasic::onLeaveChannel_slot(const RtcStats &stats)
{
	ui->agoraSteramButton->setText(start_text);
	ui->exitButton->setEnabled(true);
	blog(LOG_INFO, "onLeaveChannel");
}

void AgoraBasic::onError_slot(int err, const char *msg)
{
	blog(LOG_ERROR, "agora error code: %d", err);
}

void AgoraBasic::onUserJoined_slot(uid_t uid, int elapsed)
{
	m_lstUids.push_back(uid);

	if (m_lstUids.size() > 16) {
		blog(LOG_INFO, "userjoined begin: count=%d", m_lstUids.size());
		for (auto iter : m_lstUids) {

			blog(LOG_INFO, "uid:%u", iter);
		}
		blog(LOG_INFO, "userjoined end");
	}
}

void AgoraBasic::onUserOffline_slot(uid_t uid, int reason)
{
	for (auto iter = m_lstUids.begin(); iter != m_lstUids.end(); ++iter) {
		if (*iter == uid) {
			m_lstUids.erase(iter);
			break;
		}
	}

	int idx = -1;
	bool bFind = false;
	for (auto iter = m_lstRemoteVideoUids.begin();
		iter != m_lstRemoteVideoUids.end(); ++iter) {
		idx++;
		if (*iter == uid) {
			bFind = true;
			m_lstRemoteVideoUids.erase(iter);
			break;
		}
	}

	if (m_lstRemoteVideoUids.size() < 16) {
		int i = 0;
		uids[i++] = local_uid;
		for (auto iter : m_lstRemoteVideoUids) {
			uids[i++] = iter;
		}
		SetLiveTranscoding();
	}

	if (!bFind)
		return;

	if (idx >= REMOTE_VIDEO_COUNT)
		return;

	int count = m_lstRemoteVideoUids.size() > REMOTE_VIDEO_COUNT
		? REMOTE_VIDEO_COUNT
		: m_lstRemoteVideoUids.size();
	if (count == 0) {

		AgoraRtcEngine::GetInstance()->setupRemoteVideo(remoteVideoInfos[count].uid, nullptr);
		ResetRemoteVideoWidget(count);
		return;
	}

	int row = 1;
	int col = 1;
	if (count == 1) {
		ui->preview->setUpdatesEnabled(true);
	}
	else if (count == 2) {
		row = 2;
		col = 1;
	}
	else if (count == 3 || count == 4) {
		row = 2;
		col = 2;
	}
	else if (count == 5 || count == 6) {
		row = 2;
		col = 3;
	}
	else if (count > 6 && count <= 9) {
		row = 3;
		col = 3;
	}
	else if (count > 9 && count <= 12) {
		row = 3;
		col = 4;
	}
	else if (count > 12) {
		row = 4;
		col = 4;
	}

	ClearRemoteVideos();
	auto iter = m_lstRemoteVideoUids.begin();
	for (int i = 0; i < row; i++) {
		for (int j = 0; j < col; j++) {
			int index = i * col + j;
			if (index >= count)
				break;

			remoteVideoInfos[index].iRemoteVideoHLayout = i;
			remoteVideoInfos[index].uid = *iter;
			AgoraRtcEngine::GetInstance()->setupRemoteVideo(*iter,
				(view_t)remoteVideoInfos[index].remoteVideo->winId());

			remoteVideoHLayout[i]->addWidget(
				remoteVideoInfos[index].remoteVideo);
			++iter;
		}
	}
}

void AgoraBasic::onFirstRemoteVideoDecoded_slot(uid_t uid, int width, int height, int elapsed)
{
	m_lstRemoteVideoUids.push_back(uid);
	int count = m_lstRemoteVideoUids.size();
	count = count > REMOTE_VIDEO_COUNT ? REMOTE_VIDEO_COUNT : count;

	for (int i = 0; i < count; ++i) {
		if (remoteVideoInfos[i].uid == uid)
			return;
	}

	if (count > REMOTE_VIDEO_COUNT)
		return;

	showRemoteTimer.stop();
	showRemoteTimer.start(1000);
	
}

void AgoraBasic::onConnectionStateChanged_slot(int state, int reason)
{
	if (reason == 8 || reason == 9 ||
		reason == 6 || reason == 7) {
		joinFailed = true;
		std::string info = "";
		switch (reason)
		{
		case 8: //CONNECTION_CHANGED_INVALID_TOKEN
			info = invalidTokenlError.toStdString();
			break;
		case 9: //CONNECTION_CHANGED_TOKEN_EXPIRED
			info = invalidTokenExpiredError.toStdString();
			break;
		case 6: //CONNECTION_CHANGED_INVALID_APP_ID
			info = invalidAppidError.toStdString();
			break;
		case 7: //CONNECTION_CHANGED_INVALID_CHANNEL_NAME
			info = invalidChannelError.toStdString();
			break;
		default:
			break;
		}
		joinFailedTimer.stop();

		QMessageBox::critical(NULL, "Error", info.c_str());

		on_agoraSteramButton_clicked();
		joinFailed = false;
	}
}

void AgoraBasic::onRemoteVideoStateChanged_slot(unsigned int uid, int state, int reason, int elapsed)
{
	if (state == REMOTE_VIDEO_STATE_DECODING
		&&( reason == REMOTE_VIDEO_STATE_REASON_REMOTE_UNMUTED || reason == REMOTE_VIDEO_STATE_REASON_LOCAL_UNMUTED)) {
	    blog(LOG_INFO, "onRemoteVideoStateChanged, reason:%d, uid:%u", reason, uid);
		onFirstRemoteVideoDecoded_slot(uid, 0, 0, elapsed);
	}
	else if (state == REMOTE_VIDEO_STATE_STOPPED
		&& reason == REMOTE_VIDEO_STATE_REASON_REMOTE_MUTED) {
		blog(LOG_INFO, "onRemoteVideoStateChanged, reason:%d, uid:%u", reason, uid);
		onUserOffline_slot(uid, 0);
	}
}

void AgoraBasic::onFirstRemoteVideoFrame_slot(unsigned uid, int width, int height, int elapsed)
{
	if (m_lstRemoteVideoUids.size() == 1) {
		ui->preview->setUpdatesEnabled(false);
	}

	int count = m_lstRemoteVideoUids.size() > REMOTE_VIDEO_COUNT ? REMOTE_VIDEO_COUNT : m_lstRemoteVideoUids.size();
	for (int i = 0; i < count; ++i) {
		remoteVideoInfos[i].remoteVideo->show();
		//remoteVideoInfos[i].remoteVideo->setUpdatesEnabled(false);
	}
}

void AgoraBasic::showRemote_slot()
{
	int count = m_lstRemoteVideoUids.size();
	count = count > REMOTE_VIDEO_COUNT ? REMOTE_VIDEO_COUNT : count;


	int row = 1;
	int col = 1;
	if (count == 1) {
		ui->preview->setUpdatesEnabled(true);
	}
	else if (count == 2) {
		row = 2;
		col = 1;
	}
	else if (count == 3 || count == 4) {
		row = 2;
		col = 2;
	}
	else if (count == 5 || count == 6) {
		row = 2;
		col = 3;
	}
	else if (count > 6 && count <= 9) {
		row = 3;
		col = 3;
	}
	else if (count > 9 && count <= 12) {
		row = 3;
		col = 4;
	}
	else if (count > 12) {
		row = 4;
		col = 4;
	}

	ClearRemoteVideos();

	auto iter = m_lstRemoteVideoUids.begin();
	for (int i = 0; i < row; i++) {
		for (int j = 0; j < col; j++) {
			int index = i * col + j;
			if (index == count)
				break;
			remoteVideoInfos[index].iRemoteVideoHLayout = i;
			remoteVideoInfos[index].uid = *iter;
			remoteVideoInfos[index].remoteVideo->hide();
			AgoraRtcEngine::GetInstance()->setupRemoteVideo(*iter, (view_t)remoteVideoInfos[index].remoteVideo->winId());
			remoteVideoHLayout[i]->addWidget(remoteVideoInfos[index].remoteVideo);
			++iter;
		}
	}

	//set live transcoding
	if (m_lstRemoteVideoUids.size() > REMOTE_VIDEO_COUNT)
		return;
	int i = 0;
	uids[i++] = local_uid;
	for (auto iter : m_lstRemoteVideoUids) {
		uids[i++] = iter;
	}
	showRemoteTimer.stop();
	transcodingTimer.stop();
	transcodingTimer.start(1000);
}

void AgoraBasic::transcoding_slot()
{
	SetLiveTranscoding();
	transcodingTimer.stop();
}

void AgoraBasic::joinFailed_slot()
{
	joinFailed = true;
	QMessageBox::critical(NULL, "Error", joinFailedInfo.toStdString().c_str());
	joinFailedTimer.stop();
	on_agoraSteramButton_clicked();
	joinFailed = false;
}

void AgoraBasic::onClientRoleChanged_slot(int oldRole, int newRole)
{
	blog(LOG_INFO, "oldRole:%d, newRole:%d", oldRole, newRole);
}

void AgoraBasic::OBSEvent(enum obs_frontend_event event, void * data)
{
	if (event == OBS_FRONTEND_EVENT_EXIT) {
		AgoraBasic* basic = (AgoraBasic*)data;

		obs_display_remove_draw_callback(basic->display, DrawPreview, basic);
		QString str = basic->ui->agoraSteramButton->text();
		if (basic->stop_text.compare(str) == 0) {
			basic->on_agoraSteramButton_clicked();
		}
		obs_frontend_save();

	}
}

void AgoraBasic::onSystemCPU_slot(int cpuUsage)
{
	if (first2MinCpu.size() < 60) {
		first2MinCpu.push_back(cpuUsage);
	}
	else if (lastMinCpu.size() < 30) {
		lastMinCpu.push_back(cpuUsage);
	}

	std::string info = "";
	// calculate cpuUsage in 3 minutes
	if (lastMinCpu.size() == 30) {
		int count1 = 0;
		for (int i = 0; i < first2MinCpu.size(); ++i) {
			int cpu = first2MinCpu[i];
			char szInfo[10] = { 0 };
      snprintf(szInfo, 10, "%d ", cpu);
			info += szInfo;
			if (cpu > m_agoraToolSettings.cpuThreshold) {
				count1++;
			}
		}
		blog(LOG_INFO, "Agora System CPU Information(first 2 minutes):%s", info.c_str());
		int count2 = 0;
		info = "";
		for (int i = 0; i < lastMinCpu.size(); ++i) {
			int cpu = lastMinCpu[i];
			char szInfo[10] = { 0 };
      snprintf(szInfo, 10, "%d ", cpu);
			info += szInfo;
			if (cpu > m_agoraToolSettings.cpuThreshold) {
				count2++;
			}
		}

		blog(LOG_INFO, "Agora System CPU Information(last minute):%s", info.c_str());

		if (count1 > 25 && count2 >= 10) {
			lastMinCpu.clear();
			first2MinCpu.clear();
			QMessageBox::information(this, QString(""), cpuInformation);
		}
	}
}

