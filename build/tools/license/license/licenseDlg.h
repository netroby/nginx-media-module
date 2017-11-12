
// licenseDlg.h : ͷ�ļ�
//

#pragma once
#include "afxwin.h"


// ClicenseDlg �Ի���
class ClicenseDlg : public CDialogEx
{
// ����
public:
	ClicenseDlg(CWnd* pParent = NULL);	// ��׼���캯��

// �Ի�������
	enum { IDD = IDD_LICENSE_DIALOG };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV ֧��


// ʵ��
protected:
	HICON m_hIcon;

	// ���ɵ���Ϣӳ�亯��
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
