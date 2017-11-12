
// licenseDlg.cpp : 实现文件
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


// ClicenseDlg 对话框
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


// ClicenseDlg 消息处理程序

BOOL ClicenseDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// 设置此对话框的图标。  当应用程序主窗口不是对话框时，框架将自动
	//  执行此操作
	SetIcon(m_hIcon, TRUE);			// 设置大图标
	SetIcon(m_hIcon, FALSE);		// 设置小图标

	// TODO:  在此添加额外的初始化代码

    m_edTaskCount.SetWindowTextW(_T("50"));
    m_edRTMPChannel.SetWindowTextW(_T("200"));
    m_edRTSPChannel.SetWindowTextW(_T("200"));
    m_edHLSChannel.SetWindowTextW(_T("200"));

	return TRUE;  // 除非将焦点设置到控件，否则返回 TRUE
}

// 如果向对话框添加最小化按钮，则需要下面的代码
//  来绘制该图标。  对于使用文档/视图模型的 MFC 应用程序，
//  这将由框架自动完成。

void ClicenseDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // 用于绘制的设备上下文

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// 使图标在工作区矩形中居中
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// 绘制图标
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

//当用户拖动最小化窗口时系统调用此函数取得光标
//显示。
HCURSOR ClicenseDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}



void ClicenseDlg::OnBnClickedBtnAdd()
{
    // TODO:  在此添加控件通知处理程序代码
    CString strMacInfo = _T("");
    m_edMacAddress.GetWindowTextW(strMacInfo);
    if (0 == strMacInfo.GetLength())
    {
        AfxMessageBox(_T("请输入MAC地址"));
        return;
    }

    m_listMac.InsertString(0, strMacInfo);
    m_edMacAddress.SetWindowTextW(_T(""));
}


void ClicenseDlg::OnBnClickedBtnDel()
{
    // TODO:  在此添加控件通知处理程序代码
    int index = m_listMac.GetCurSel();
    if (LB_ERR == index)
    {
        AfxMessageBox(_T("请选择需要删除的MAC地址"));
        return;
    }
    m_listMac.DeleteString(index);
}


void ClicenseDlg::OnBnClickedOk()
{
    // TODO:  在此添加控件通知处理程序代码
    int nCount = m_listMac.GetCount();
    if (0 >= nCount)
    {
        AfxMessageBox(_T("请输入MAC地址"));
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

    XMLDeclaration* declaration = doc.NewDeclaration();//添加xml文件头申明
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

    // 创建孙元素<area>  
    XMLElement* hash = doc.NewElement("hash");
    hash->SetText((char*)&szHashCodeStr[0]);
    license->InsertEndChild(hash);

    // 输出XML至文件  
    doc.SaveFile("license.xml");
    AfxMessageBox(_T("licens 文件已经生成"));
    return ;
}


void ClicenseDlg::OnBnClickedCancel()
{
    // TODO:  在此添加控件通知处理程序代码
    CDialogEx::OnCancel();
}


void ClicenseDlg::OnEnChangeEditTask()
{
    // TODO:  如果该控件是 RICHEDIT 控件，它将不
    // 发送此通知，除非重写 CDialogEx::OnInitDialog()
    // 函数并调用 CRichEditCtrl().SetEventMask()，
    // 同时将 ENM_CHANGE 标志“或”运算到掩码中。

    // TODO:  在此添加控件通知处理程序代码
    CString strTaskCount = _T("");
    m_edTaskCount.GetWindowTextW(strTaskCount);
    unsigned long nTaskcount = _tcstoul(strTaskCount.GetBuffer(0),NULL,10);
    if (nTaskcount > 200)
    {
        AfxMessageBox(_T("单台最大任务数200"));
        m_edTaskCount.SetWindowTextW(_T("50"));
        return;
    }
}


void ClicenseDlg::OnEnChangeEditRtmp()
{
    // TODO:  如果该控件是 RICHEDIT 控件，它将不
    // 发送此通知，除非重写 CDialogEx::OnInitDialog()
    // 函数并调用 CRichEditCtrl().SetEventMask()，
    // 同时将 ENM_CHANGE 标志“或”运算到掩码中。

    // TODO:  在此添加控件通知处理程序代码
    CString strRTMPCount = _T("");
    m_edRTMPChannel.GetWindowTextW(strRTMPCount);
    unsigned long nTaskcount = _tcstoul(strRTMPCount.GetBuffer(0), NULL, 10);
    if (nTaskcount > 2000)
    {
        AfxMessageBox(_T("单台最大RTMP通道数2000"));
        m_edRTMPChannel.SetWindowTextW(_T("200"));
        return;
    }
}


void ClicenseDlg::OnEnChangeEditRtsp()
{
    // TODO:  如果该控件是 RICHEDIT 控件，它将不
    // 发送此通知，除非重写 CDialogEx::OnInitDialog()
    // 函数并调用 CRichEditCtrl().SetEventMask()，
    // 同时将 ENM_CHANGE 标志“或”运算到掩码中。

    // TODO:  在此添加控件通知处理程序代码
    CString strRTSPCount = _T("");
    m_edRTSPChannel.GetWindowTextW(strRTSPCount);
    unsigned long nTaskcount = _tcstoul(strRTSPCount.GetBuffer(0), NULL, 10);
    if (nTaskcount > 2000)
    {
        AfxMessageBox(_T("单台最大RTSP通道数2000"));
        m_edRTSPChannel.SetWindowTextW(_T("200"));
        return;
    }
}


void ClicenseDlg::OnEnChangeEditHls()
{
    // TODO:  如果该控件是 RICHEDIT 控件，它将不
    // 发送此通知，除非重写 CDialogEx::OnInitDialog()
    // 函数并调用 CRichEditCtrl().SetEventMask()，
    // 同时将 ENM_CHANGE 标志“或”运算到掩码中。

    // TODO:  在此添加控件通知处理程序代码
    CString strHLSCount = _T("");
    m_edHLSChannel.GetWindowTextW(strHLSCount);
    unsigned long nTaskcount = _tcstoul(strHLSCount.GetBuffer(0), NULL, 10);
    if (nTaskcount > 2000)
    {
        AfxMessageBox(_T("单台最大HLS通道数2000"));
        m_edHLSChannel.SetWindowTextW(_T("200"));
        return;
    }
}
