
// licenseDlg.h : 头文件
//

#pragma once
#include "afxwin.h"


// ClicenseDlg 对话框
class ClicenseDlg : public CDialogEx
{
// 构造
public:
	ClicenseDlg(CWnd* pParent = NULL);	// 标准构造函数

// 对话框数据
	enum { IDD = IDD_LICENSE_DIALOG };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV 支持


// 实现
protected:
	HICON m_hIcon;

	// 生成的消息映射函数
	virtual BOOL OnInitDialog();
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
public:
    CEdit m_edMacAddress;
    CListBox m_listMac;
    afx_msg void OnBnClickedBtnAdd();
    afx_msg void OnBnClickedBtnDel();
    afx_msg void OnBnClickedOk();
    afx_msg void OnBnClickedCancel();
    afx_msg void OnEnChangeEdit2();
    CEdit m_edTaskCount;
    CEdit m_edRTMPChannel;
    CEdit m_edRTSPChannel;
    CEdit m_edHLSChannel;
    afx_msg void OnEnChangeEditTask();
    afx_msg void OnEnChangeEditRtmp();
    afx_msg void OnEnChangeEditRtsp();
    afx_msg void OnEnChangeEditHls();
};
