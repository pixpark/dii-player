#pragma once

#include "dii_player.h"
#include "DlgVideo.h"
#include "Resource.h"

using namespace dii_media_kit;
 
// DlgDiiPlayer  

class DlgDiiPlayer : public CDialog
{
	DECLARE_DYNAMIC(DlgDiiPlayer)

public:
	DlgDiiPlayer(CWnd* pParent = NULL);   
	virtual ~DlgDiiPlayer();

 
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG_PLAYER };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    
	virtual BOOL OnInitDialog();
	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnBnClickedStop();
	afx_msg void OnBnClickedPause();
	afx_msg void OnBnClickedResume();
	afx_msg void OnBnClickedSeek();
	afx_msg void OnBnClickedPressTest();
	afx_msg void OnBnClickedStart();

	CEdit	m_editUrl;
	CButton m_btnStart;
	CButton m_btnStop;
	CButton m_btnPause;
	CButton m_btnResume;
	CString m_strUrl;
	CString m_strPos;
	CStatic m_staticCaptrue;

	CString m_strW;
	CString m_strH;
	CString m_strPause;
	CString m_strEvent;


private:
	char	m_file_url[1024];

	DlgVideo		*m_pDlgVideoMain;
public:
	afx_msg void OnEnChangeEditUrlFile();
	afx_msg void OnStnClickedStaticDisplay();
	afx_msg void OnEnChangeEditPos();
	afx_msg void OnBnClickedResize();
	afx_msg void OnEnChangeEditWidth();
	afx_msg void OnEnChangeEditHeight();
	afx_msg void OnEnChangeEventFinished();
	afx_msg void OnBnClickedDiiChange();
	afx_msg void OnEnChangeEditPause();
	void testMic();
};
