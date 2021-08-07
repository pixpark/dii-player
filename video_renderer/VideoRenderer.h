#pragma once

#include "video_renderer.h"
#include "webrtc/base/scoped_ref_ptr.h"
#include "webrtc/typedefs.h"
#include "pipeline_render.h"

namespace dii_media_kit {
namespace test {
//---------------------------------------------------------------------------------------
// TCP���ඨ��
class  D3DVideoRenderer : public VideoRenderer
{
public:
	D3DVideoRenderer(void);
	virtual ~D3DVideoRenderer(void);

	virtual int Create(const void* hParentWnd/*HWND*/, int nWidth, int nHeight, int nFrameRate);
	//�ر�
	virtual void Destroy(void);
	//����&��ʾ
	virtual int	Render(unsigned char*pData, int nLen);

	void OnFrame(const cricket::VideoFrame& frame) override;
private:
	CRendererPipeline*		m_pEncodingPipeline;
	int						m_nWidth;
	int						m_nHeight;
	const void*				m_hParentWnd;
};
}  // namespace test
}  // namespace dii_media_kit
