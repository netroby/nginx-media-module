
// licenseDlg.cpp : ʵ���ļ�
//

#include "stdafx.h"
#include "license.h"
#include "licenseDlg.h"
#include "afxdialogex.h"
#include "tinyxml2.h"
#include "cmd5.h"
#include<atlconv.h>

using namespace tinyxml2;
#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// ClicenseDlg �Ի���
#define LICENSE_CONFUSE_CODE            "ALL@MEDIA#0608"
#define LICENSE_HASH_CODE_LEN          16
#define LICENSE_HASH_CODE_STR_LEN      32

ClicenseDlg::ClicenseDlg(CWnd* pParent /*=NULL*/)
	: CDialogEx(ClicenseDlg::IDD, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void ClicenseDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_EDIT_MAC, m_edMacAddress);
    DDX_Control(pDX, IDC_LIST_MAC, m_listMac);
    DDX_Control(pDX, IDC_EDIT_TASK, m_edTaskCount);
    DDX_Control(pDX, IDC_EDIT_RTMP, m_edRTMPChannel);
    DDX_Control(pDX, IDC_EDIT_RTSP, m_edRTSPChannel);
    DDX_Control(pDX, IDC_EDIT_HLS, m_edHLSChannel);
}

BEGIN_MESSAGE_MAP(ClicenseDlg, CDialogEx)
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
    ON_BN_CLICKED(IDC_BTN_ADD, &ClicenseDlg::OnBnClickedBtnAdd)
    ON_BN_CLICKED(IDC_BTN_DEL, &ClicenseDlg::OnBnClickedBtnDel)
    ON_BN_CLICKED(IDOK, &ClicenseDlg::OnBnClickedOk)
    ON_BN_CLICKED(IDCANCEL, &ClicenseDlg::OnBnClickedCancel)
    ON_EN_CHANGE(IDC_EDIT_TASK, &ClicenseDlg::OnEnChangeEditTask)
    ON_EN_CHANGE(IDC_EDIT_RTMP, &ClicenseDlg::OnEnChangeEditRtmp)
    ON_EN_CHANGE(IDC_EDIT_RTSP, &ClicenseDlg::OnEnChangeEditRtsp)
    ON_EN_CHANGE(IDC_EDIT_HLS, &ClicenseDlg::OnEnChangeEditHls)
END_MESSAGE_MAP()


// ClicenseDlg ��Ϣ�������

BOOL ClicenseDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// ���ô˶Ի����ͼ�ꡣ  ��Ӧ�ó��������ڲ��ǶԻ���ʱ����ܽ��Զ�
	//  ִ�д˲���
	SetIcon(m_hIcon, TRUE);			// ���ô�ͼ��
	SetIcon(m_hIcon, FALSE);		// ����Сͼ��

	// TODO:  �ڴ���Ӷ���ĳ�ʼ������

    m_edTaskCount.SetWindowTextW(_T("50"));
    m_edRTMPChannel.SetWindowTextW(_T("200"));
    m_edRTSPChannel.SetWindowTextW(_T("200"));
    m_edHLSChannel.SetWindowTextW(_T("200"));

	return TRUE;  // ���ǽ��������õ��ؼ������򷵻� TRUE
}

// �����Ի��������С����ť������Ҫ����Ĵ���
//  �����Ƹ�ͼ�ꡣ  ����ʹ���ĵ�/��ͼģ�͵� MFC Ӧ�ó���
//  �⽫�ɿ���Զ���ɡ�

void ClicenseDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // ���ڻ��Ƶ��豸������

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// ʹͼ���ڹ����������о���
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// ����ͼ��
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

//���û��϶���С������ʱϵͳ���ô˺���ȡ�ù��
//��ʾ��
HCURSOR ClicenseDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}



void ClicenseDlg::OnBnClickedBtnAdd()
{
    // TODO:  �ڴ���ӿؼ�֪ͨ����������
    CString strMacInfo = _T("");
    m_edMacAddress.GetWindowTextW(strMacInfo);
    if (0 == strMacInfo.GetLength())
    {
        AfxMessageBox(_T("������MAC��ַ"));
        return;
    }

    m_listMac.InsertString(0, strMacInfo);
    m_edMacAddress.SetWindowTextW(_T(""));
}


void ClicenseDlg::OnBnClickedBtnDel()
{
    // TODO:  �ڴ���ӿؼ�֪ͨ����������
    int index = m_listMac.GetCurSel();
    if (LB_ERR == index)
    {
        AfxMessageBox(_T("��ѡ����Ҫɾ����MAC��ַ"));
        return;
    }
    m_listMac.DeleteString(index);
}


void ClicenseDlg::OnBnClickedOk()
{
    // TODO:  �ڴ���ӿؼ�֪ͨ����������
    int nCount = m_listMac.GetCount();
    if (0 >= nCount)
    {
        AfxMessageBox(_T("������MAC��ַ"));
        return;
    }
    int i = 0,j =0;
    unsigned char szHashCode[LICENSE_HASH_CODE_LEN] = { 0 }; 
    unsigned char szHashCodeStr[LICENSE_HASH_CODE_STR_LEN+1] = { 0 };
    CString strKey = _T("");
    CString strHash = _T("");
    CString strMacInfo = _T("");
    CString strTaskCount = _T("");
    CString strRTSPCount = _T("");
    CString strHLSCount = _T("");
    CString strRTMPCount = _T("");
    m_edRTMPChannel.GetWindowTextW(strRTMPCount);
    m_edHLSChannel.GetWindowTextW(strHLSCount);
    m_edRTSPChannel.GetWindowTextW(strRTSPCount);
    m_edTaskCount.GetWindowTextW(strTaskCount);

    unsigned long nTaskcount = _tcstoul(strTaskCount.GetBuffer(0), NULL, 10);
    unsigned long nRTMPcount = _tcstoul(strRTMPCount.GetBuffer(0), NULL, 10);
    unsigned long nRTSPcount = _tcstoul(strRTSPCount.GetBuffer(0), NULL, 10);
    unsigned long nHLScount = _tcstoul(strHLSCount.GetBuffer(0), NULL, 10);

    strKey.Format(_T("%d:%d:%d:%d:%s"), nTaskcount, nRTMPcount, nRTSPcount, nHLScount, _T(LICENSE_CONFUSE_CODE));

    for ( i = 0; i < nCount; i++)
    {
        m_listMac.GetText(i, strMacInfo);
        strKey += _T(":");
        strKey += strMacInfo;
    }
    USES_CONVERSION;
    char *pChStr2 = (char*)W2A(strKey);
    CMD5 objMD5;
    objMD5.MD5Update((const unsigned char *)pChStr2, strlen(pChStr2));
    objMD5.MD5Final(szHashCode);
    for (j = 0; j < LICENSE_HASH_CODE_LEN; j++) {
        _snprintf((char*)&szHashCodeStr[j * 2], 2, "%02X", szHashCode[j]);
    }

    tinyxml2::XMLDocument doc;

    XMLDeclaration* declaration = doc.NewDeclaration();//���xml�ļ�ͷ����
    doc.InsertFirstChild(declaration);

    XMLElement* license = doc.NewElement("license");
    doc.InsertEndChild(license);

    XMLElement* hostlist = doc.NewElement("hostlist");
    license->InsertEndChild(hostlist);
    XMLElement* host = NULL;
    for ( i = 0; i < nCount; i++)
    {
        m_listMac.GetText(i, strMacInfo);
        pChStr2 = (char*)W2A(strMacInfo);
        host = doc.NewElement("host");
        hostlist->InsertEndChild(host);
        host->SetAttribute("mac", pChStr2);
    }

    XMLElement* ability = doc.NewElement("ability");
    pChStr2 = (char*)W2A(strRTMPCount);
    ability->SetAttribute("rtmp", pChStr2);
    pChStr2 = (char*)W2A(strRTSPCount);
    ability->SetAttribute("rtsp", pChStr2);
    pChStr2 = (char*)W2A(strHLSCount);
    ability->SetAttribute("hls", pChStr2);
    pChStr2 = (char*)W2A(strTaskCount);
    ability->SetAttribute("task", pChStr2);

    license->InsertEndChild(ability);

    // ������Ԫ��<area>  
    XMLElement* hash = doc.NewElement("hash");
    hash->SetText((char*)&szHashCodeStr[0]);
    license->InsertEndChild(hash);

    // ���XML���ļ�  
    doc.SaveFile("license.xml");
    AfxMessageBox(_T("licens �ļ��Ѿ�����"));
    return ;
}


void ClicenseDlg::OnBnClickedCancel()
{
    // TODO:  �ڴ���ӿؼ�֪ͨ����������
    CDialogEx::OnCancel();
}


void ClicenseDlg::OnEnChangeEditTask()
{
    // TODO:  ����ÿؼ��� RICHEDIT �ؼ���������
    // ���ʹ�֪ͨ��������д CDialogEx::OnInitDialog()
    // ���������� CRichEditCtrl().SetEventMask()��
    // ͬʱ�� ENM_CHANGE ��־�������㵽�����С�

    // TODO:  �ڴ���ӿؼ�֪ͨ����������
    CString strTaskCount = _T("");
    m_edTaskCount.GetWindowTextW(strTaskCount);
    unsigned long nTaskcount = _tcstoul(strTaskCount.GetBuffer(0),NULL,10);
    if (nTaskcount > 200)
    {
        AfxMessageBox(_T("��̨���������200"));
        m_edTaskCount.SetWindowTextW(_T("50"));
        return;
    }
}


void ClicenseDlg::OnEnChangeEditRtmp()
{
    // TODO:  ����ÿؼ��� RICHEDIT �ؼ���������
    // ���ʹ�֪ͨ��������д CDialogEx::OnInitDialog()
    // ���������� CRichEditCtrl().SetEventMask()��
    // ͬʱ�� ENM_CHANGE ��־�������㵽�����С�

    // TODO:  �ڴ���ӿؼ�֪ͨ����������
    CString strRTMPCount = _T("");
    m_edRTMPChannel.GetWindowTextW(strRTMPCount);
    unsigned long nTaskcount = _tcstoul(strRTMPCount.GetBuffer(0), NULL, 10);
    if (nTaskcount > 2000)
    {
        AfxMessageBox(_T("��̨���RTMPͨ����2000"));
        m_edRTMPChannel.SetWindowTextW(_T("200"));
        return;
    }
}


void ClicenseDlg::OnEnChangeEditRtsp()
{
    // TODO:  ����ÿؼ��� RICHEDIT �ؼ���������
    // ���ʹ�֪ͨ��������д CDialogEx::OnInitDialog()
    // ���������� CRichEditCtrl().SetEventMask()��
    // ͬʱ�� ENM_CHANGE ��־�������㵽�����С�

    // TODO:  �ڴ���ӿؼ�֪ͨ����������
    CString strRTSPCount = _T("");
    m_edRTSPChannel.GetWindowTextW(strRTSPCount);
    unsigned long nTaskcount = _tcstoul(strRTSPCount.GetBuffer(0), NULL, 10);
    if (nTaskcount > 2000)
    {
        AfxMessageBox(_T("��̨���RTSPͨ����2000"));
        m_edRTSPChannel.SetWindowTextW(_T("200"));
        return;
    }
}


void ClicenseDlg::OnEnChangeEditHls()
{
    // TODO:  ����ÿؼ��� RICHEDIT �ؼ���������
    // ���ʹ�֪ͨ��������д CDialogEx::OnInitDialog()
    // ���������� CRichEditCtrl().SetEventMask()��
    // ͬʱ�� ENM_CHANGE ��־�������㵽�����С�

    // TODO:  �ڴ���ӿؼ�֪ͨ����������
    CString strHLSCount = _T("");
    m_edHLSChannel.GetWindowTextW(strHLSCount);
    unsigned long nTaskcount = _tcstoul(strHLSCount.GetBuffer(0), NULL, 10);
    if (nTaskcount > 2000)
    {
        AfxMessageBox(_T("��̨���HLSͨ����2000"));
        m_edHLSChannel.SetWindowTextW(_T("200"));
        return;
    }
}
