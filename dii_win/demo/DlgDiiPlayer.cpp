// DlgDiiPlayer.cpp : 实现文件
//

#include "stdafx.h"
#include "DlgDiiPlayer.h"

using namespace dii_media_kit;

// DlgDiiPlayer 对话框
IMPLEMENT_DYNAMIC(DlgDiiPlayer, CDialog)

static int index = 0;
static int count = 10;
DiiPlayerCallback* g_callback[10] = { 0 };
DiiPlayer*  m_player[10] = {0};
static int save_static_pic = 0;
DlgDiiPlayer* g_dlg = NULL;

bool g_set = true;
void dii_trackEventCallback(DiiTrackEvent event)
{
	int sd = 0;
	sd = 1;
	sd = 3;
}

DlgDiiPlayer::DlgDiiPlayer(CWnd* pParent /*=NULL*/)
	:m_strUrl(_T("https://outin-cf17324cfc8311e9a7ef00163e1c7426.oss-cn-shanghai.aliyuncs.com/800346f18b254bc7a7e6f097b7a2d54a/407b7965effe4f1db90379e5748df848-814c36e9ec9caffbb05ac842cce93933-ld.m3u8"))
	,m_strPos(_T("0"))
	,m_strW(_T("760"))
	,m_strH(_T("570"))
	,m_strEvent(_T("0"))
	,m_strPause(_T("0"))
	, CDialog(IDD_DIALOG_PLAYER, pParent)
{
	if (g_set == false) {
		g_set = true;
		dii_media_kit::DiiMediaKit::SetTraceLog("./dii_player.log", dii_media_kit::LOG_INFO);
		dii_media_kit::DiiMediaKit::SetEventTrackinglCallback(dii_trackEventCallback);
	}


	strcpy_s(m_file_url, "./sample_video.mp4");
	// http://1400277792.vod2.myqcloud.com/d431dfd1vodtranscq1400277792/7a1105f75285890795590747503/v.f220.m3u8 //mp3
	// http://1400277792.vod2.myqcloud.com/d431dfd1vodtranscq1400277792/70cf8c375285890795590359306/v.f220.m3u8 //mp4 clock
	//http://1400277792.vod2.myqcloud.com/d431dfd1vodtranscq1400277792/7077f9175285890795528907180/v.f210.m3u8  //1min
	//https://outin-cf17324cfc8311e9a7ef00163e1c7426.oss-cn-shanghai.aliyuncs.com/800346f18b254bc7a7e6f097b7a2d54a/407b7965effe4f1db90379e5748df848-814c36e9ec9caffbb05ac842cce93933-ld.m3u8
	//http://1400277792.vod2.myqcloud.com/d431dfd1vodtranscq1400277792/70cf8c375285890795590359306/v.f220.m3u8 //tengxu
	g_dlg = this;
}

DlgDiiPlayer::~DlgDiiPlayer()
{
}

void DlgDiiPlayer::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_EDIT_URL_FILE, m_editUrl);
	DDX_Text(pDX, IDC_EDIT_URL_FILE, m_strUrl);
	DDX_Text(pDX, IDC_EDIT_POS, m_strPos);
	DDX_Text(pDX, IDC_EDIT_WIDTH, m_strW);
	DDX_Text(pDX, IDC_EDIT_HEIGHT, m_strH);
	DDX_Text(pDX, IDC_EDIT_PAUSE, m_strPause);	
	DDX_Text(pDX, IDC_EVENT_FINISHED, m_strEvent);
	DDX_Control(pDX, ID_START, m_btnStart);
	DDX_Control(pDX, ID_STOP, m_btnStop);
	DDX_Control(pDX, IDC_STATIC_DISPLAY, m_staticCaptrue);
}

BOOL DlgDiiPlayer::OnInitDialog()
{
	CDialog::OnInitDialog();

	{// Video player
		m_pDlgVideoMain = new DlgVideo(this);
		m_pDlgVideoMain->Create(DlgVideo::IDD, this);

		CRect rc;
		m_staticCaptrue.GetWindowRect(rc);
		m_staticCaptrue.ShowWindow(SW_HIDE);
		ScreenToClient(rc);
		m_pDlgVideoMain->SetWindowPos(NULL, rc.left, rc.top, rc.Width(), rc.Height(), SWP_SHOWWINDOW);
	}

	return TRUE;  // return TRUE unless you set the focus to a control
}

BEGIN_MESSAGE_MAP(DlgDiiPlayer, CDialog)
	ON_BN_CLICKED(ID_STOP, &DlgDiiPlayer::OnBnClickedStop)
	ON_BN_CLICKED(IDPAUSE, &DlgDiiPlayer::OnBnClickedPause)
	ON_BN_CLICKED(IDRESUME, &DlgDiiPlayer::OnBnClickedResume)
	ON_BN_CLICKED(IDSEEK, &DlgDiiPlayer::OnBnClickedSeek)
	ON_BN_CLICKED(ID_PRESS_TEST, &DlgDiiPlayer::OnBnClickedPressTest)
	ON_BN_CLICKED(ID_START, &DlgDiiPlayer::OnBnClickedStart)
	ON_EN_CHANGE(IDC_EDIT_URL, &DlgDiiPlayer::OnEnChangeEditUrlFile)
	ON_STN_CLICKED(IDC_STATIC_DISPLAY, &DlgDiiPlayer::OnStnClickedStaticDisplay)
	ON_EN_CHANGE(IDC_EDIT_POS, &DlgDiiPlayer::OnEnChangeEditPos)
	ON_BN_CLICKED(ID_RESIZE, &DlgDiiPlayer::OnBnClickedResize)
	ON_EN_CHANGE(IDC_EDIT_WIDTH, &DlgDiiPlayer::OnEnChangeEditWidth)
	ON_EN_CHANGE(IDC_EDIT_HEIGHT, &DlgDiiPlayer::OnEnChangeEditHeight)
	ON_EN_CHANGE(IDC_EVENT_FINISHED, &DlgDiiPlayer::OnEnChangeEventFinished)
	ON_BN_CLICKED(ID_DII_CHANGE, &DlgDiiPlayer::OnBnClickedDiiChange)
	ON_EN_CHANGE(IDC_EDIT_PAUSE, &DlgDiiPlayer::OnEnChangeEditPause)
END_MESSAGE_MAP()


// DlgDiiPlayer 消息处理程序

void DlgDiiPlayer::OnBnClickedStop()
{
	// TODO: 在此添加控件通知处理程序代码
	if (m_player[index]) {
		delete m_player[index];
		m_player[index] = NULL;
		//m_player[index]->Stop();
	}

	//dii_media_kit::DiiMediaKit::SetEventTrackinglCallback(dii_trackEventCallback);
	//dii_media_kit::DiiMediaKit::SetEventTrackinglCallback(dii_trackEventCallback);
}


void DlgDiiPlayer::OnBnClickedPause()
{
	if (m_player[index]) {
		m_player[index]->Pause();
	}
}


void DlgDiiPlayer::OnBnClickedResume()
{
	if (m_player[index]) {
		m_player[index]->Resume();
	}
}


void DlgDiiPlayer::OnBnClickedSeek()
{
	UpdateData(TRUE);
	char pos[16] = { 0 };
	int poslen = m_strPos.GetLength();
	for (int i = 0; i <= poslen; i++) {
		pos[i] = m_strPos.GetAt(i);
	}

	if (m_player[index]) {
		m_player[index]->Seek(atoi(pos)*1000);
	}
}


void DlgDiiPlayer::OnBnClickedPressTest()
{
	// TODO: 在此添加控件通知处理程序代码
	while (1) {
		for (int i = 0; i < 10; i++) {
			index = i;
			OnBnClickedStart();
			Sleep(200);
			OnBnClickedStop();
			Sleep(100);
		}

	}
	

}


//test function for save local bmp
int bmp_write(unsigned char *image, int imageWidth, int imageHeight, char *filename)
{
	unsigned char header[54] = {
		0x42, 0x4d, 0, 0, 0, 0, 0, 0, 0, 0,
		54, 0, 0, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 32, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0
	};

	long file_size = (long)imageWidth * (long)imageHeight * 4 + 54;
	header[2] = (unsigned char)(file_size & 0x000000ff);
	header[3] = (file_size >> 8) & 0x000000ff;
	header[4] = (file_size >> 16) & 0x000000ff;
	header[5] = (file_size >> 24) & 0x000000ff;

	long width = imageWidth;
	header[18] = width & 0x000000ff;
	header[19] = (width >> 8) & 0x000000ff;
	header[20] = (width >> 16) & 0x000000ff;
	header[21] = (width >> 24) & 0x000000ff;

	long height = imageHeight;
	header[22] = height & 0x000000ff;
	header[23] = (height >> 8) & 0x000000ff;
	header[24] = (height >> 16) & 0x000000ff;
	header[25] = (height >> 24) & 0x000000ff;

	char fname_bmp[128];
	sprintf_s(fname_bmp, "%s.bmp", filename);

	FILE *fp = NULL;
	fopen_s(&fp, fname_bmp, "wb");
	if (!fp)
		return -1;

	fwrite(header, sizeof(unsigned char), 54, fp);
	fwrite(image, sizeof(unsigned char), (size_t)(long)imageWidth * imageHeight * 4, fp);

	fclose(fp);
	return 0;
}

void OnVideoFrame(DiiVideoFrame& frame, void* custom)
{
	return;
	static int save_num[10] = { 0 };
	DiiPlayer* player = (DiiPlayer*)custom;
	int i = 0;
	for (i = 0; i < count; i++) {
		if (player == m_player[i])
			break;
	}

	if ( 0 == save_static_pic) {
		if (save_num[i]++ > 2) {}//return;

		char bpm_file[16] = { 0 };
		sprintf_s(bpm_file, "%d_%d_%d", i, save_num[i], player);
		bmp_write((unsigned char*)frame.rgba_buffer, frame.width, frame.height, bpm_file);
	}
	else {
		char bpm_file[32] = { 0 };
		sprintf_s(bpm_file, "%d_%d_%d_pause_resize", i, save_num[i], player);
		bmp_write((unsigned char*)frame.rgba_buffer, frame.width, frame.height, bpm_file);
		save_static_pic = 0;
	}
}

void OnEventCallback(int eventType, int code, const char* msg, void* custom_data)
{
	return;

	g_dlg->UpdateData(TRUE);
	DiiPlayer* player = (DiiPlayer*)custom_data;
	char pos[16] = { 0 };
	int poslen = g_dlg->m_strEvent.GetLength();
	for (int i = 0; i <= poslen; i++) {
		pos[i] = g_dlg->m_strEvent.GetAt(i);
	}
	
	int num = atoi(pos) + 1;
	g_dlg->m_strEvent.Format(_T("%d_e"), eventType);
	g_dlg->UpdateData(FALSE);
	 
}


void DlgDiiPlayer::OnBnClickedStart()
{
	// TODO: 在此添加控件通知处理程序代码
	if (!m_player[index]) {
		m_player[index] = new DiiPlayer(m_pDlgVideoMain->m_hWnd);
		g_callback[index] = new DiiPlayerCallback();
		g_callback[index]->video_frame_callback = OnVideoFrame;
		g_callback[index]->state_callback = OnEventCallback;
		g_callback[index]->custom_data = this;
		m_player[index]->SetPlayerCallback(g_callback[index]);
	}

	UpdateData(TRUE);
	char url[1024] = {0};
	int fnlen = m_strUrl.GetLength();
	for (int i = 0; i <= fnlen; i++) {
		url[i] = m_strUrl.GetAt(i);
	}
	char pos[16] = { 0 };
	int poslen = m_strPos.GetLength();
	for (int i = 0; i <= poslen; i++) {
		pos[i] = m_strPos.GetAt(i);
	}
	char pause[4] = { 0 };
	int len = m_strPause.GetLength();
	for (int i = 0; i <= len; i++) {
		pause[i] = m_strPause.GetAt(i);
	}

	if (m_player[index]) {
		//m_player[index]->FileDuration(url);
		char * url_ = url;
		url_ = (char *)"https://outin-cf17324cfc8311e9a7ef00163e1c7426.oss-cn-shanghai.aliyuncs.com/800346f18b254bc7a7e6f097b7a2d54a/407b7965effe4f1db90379e5748df848-814c36e9ec9caffbb05ac842cce93933-ld.m3u8";
		m_player[index]->SetUserInfo(dii_radar::_Role_Teacher, (char *)"userid");
		m_player[index]->Start(url_, atoi(pos)*1000, atoi(pause));
		//m_player[index]->Duration();
		
		//m_player[index]->Pause();
		//m_player[index]->Start(url, 0);
	}
}


void DlgDiiPlayer::OnEnChangeEditUrlFile()
{
	// TODO:  如果该控件是 RICHEDIT 控件，它将不
	// 发送此通知，除非重写 CDialog::OnInitDialog()
	// 函数并调用 CRichEditCtrl().SetEventMask()，
	// 同时将 ENM_CHANGE 标志“或”运算到掩码中。

	// TODO:  在此添加控件通知处理程序代码
}


void DlgDiiPlayer::OnStnClickedStaticDisplay()
{
	// TODO: 在此添加控件通知处理程序代码
}


void DlgDiiPlayer::OnEnChangeEditPos()
{
	// TODO:  如果该控件是 RICHEDIT 控件，它将不
	// 发送此通知，除非重写 CDialog::OnInitDialog()
	// 函数并调用 CRichEditCtrl().SetEventMask()，
	// 同时将 ENM_CHANGE 标志“或”运算到掩码中。

	// TODO:  在此添加控件通知处理程序代码
}


void DlgDiiPlayer::OnBnClickedResize()
{
	// TODO: 在此添加控件通知处理程序代码
	UpdateData(TRUE);
	char pos[16] = { 0 };
	int poslen = m_strW.GetLength();
	for (int i = 0; i <= poslen; i++) {
		pos[i] = m_strW.GetAt(i);
	}
	int w = atoi(pos);

	memset(pos, 0, 16);
	poslen = m_strH.GetLength();
	for (int i = 0; i <= poslen; i++) {
		pos[i] = m_strH.GetAt(i);
	}
	int h = atoi(pos);

	save_static_pic = 1;
}


void DlgDiiPlayer::OnEnChangeEditWidth()
{
	// TODO:  如果该控件是 RICHEDIT 控件，它将不
	// 发送此通知，除非重写 CDialog::OnInitDialog()
	// 函数并调用 CRichEditCtrl().SetEventMask()，
	// 同时将 ENM_CHANGE 标志“或”运算到掩码中。

	// TODO:  在此添加控件通知处理程序代码
}


void DlgDiiPlayer::OnEnChangeEditHeight()
{
	// TODO:  如果该控件是 RICHEDIT 控件，它将不
	// 发送此通知，除非重写 CDialog::OnInitDialog()
	// 函数并调用 CRichEditCtrl().SetEventMask()，
	// 同时将 ENM_CHANGE 标志“或”运算到掩码中。

	// TODO:  在此添加控件通知处理程序代码
}


void DlgDiiPlayer::OnEnChangeEventFinished()
{
	// TODO:  如果该控件是 RICHEDIT 控件，它将不
	// 发送此通知，除非重写 CDialog::OnInitDialog()
	// 函数并调用 CRichEditCtrl().SetEventMask()，
	// 同时将 ENM_CHANGE 标志“或”运算到掩码中。

	// TODO:  在此添加控件通知处理程序代码
}

static int change = 0;
void DlgDiiPlayer::OnBnClickedDiiChange()
{

	char speaker[256] = "{0.0.0.00000000}.{ed95eb49-5845-476a-87c0-c0ca1e559b76}";//耳机
	char self[256] = "{0.0.0.00000000}.{c06237bf-2104-498f-a45a-f7f522d08674}"; //自带
	char speaker2[256] = "{0.0.0.00000000}.{2d6914a0-b878-48ee-bab0-5b71a1f2e541}";// 耳机2
	// TODO: 在此添加控件通知处理程序代码
	if (0 == change) {
		DiiPlayer::SetPlayoutVolume(200);
		DiiPlayer::SetPlayoutDevice(speaker);
		change = 1;
	}
	else
	{
		DiiPlayer::SetPlayoutVolume(255);
		DiiPlayer::SetPlayoutDevice(self);
		change = 0;
	}

}


void DlgDiiPlayer::OnEnChangeEditPause()
{
	// TODO:  如果该控件是 RICHEDIT 控件，它将不
	// 发送此通知，除非重写 CDialog::OnInitDialog()
	// 函数并调用 CRichEditCtrl().SetEventMask()，
	// 同时将 ENM_CHANGE 标志“或”运算到掩码中。

	// TODO:  在此添加控件通知处理程序代码
}
 