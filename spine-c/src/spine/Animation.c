/*******************************************************************************
 * Copyright (c) 2013, Esoteric Software
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/

#include <spine/Animation.h>
#include <math.h>
#include <spine/extension.h>

Animation* Animation_new (int timelineCount) {
	Animation* self = NEW(Animation);
	self->timelineCount = timelineCount;
	self->timelines = MALLOC(Timeline*, timelineCount);
	return self;
}

void Animation_free (Animation* self) {
	int i;
	for (i = 0; i < self->timelineCount; ++i)
		Timeline_free(self->timelines[i]);
	FREE(self->timelines);
	FREE(self);
}

void Animation_apply (const Animation* self, Skeleton* skeleton, float time, int/*bool*/loop) {
	if (loop && self->duration) time = fmodf(time, self->duration);

	int i, n = self->timelineCount;
	for (i = 0; i < n; ++i)
		Timeline_apply(self->timelines[i], skeleton, time, 1);
}

void Animation_mix (const Animation* self, Skeleton* skeleton, float time, int/*bool*/loop, float alpha) {
	if (loop && self->duration) time = fmodf(time, self->duration);

	int i, n = self->timelineCount;
	for (i = 0; i < n; ++i)
		Timeline_apply(self->timelines[i], skeleton, time, alpha);
}

/**/

void _Timeline_init (Timeline* self) {
	CONST_CAST(_TimelineVtable*, self->vtable) = NEW(_TimelineVtable);
}

void _Timeline_deinit (Timeline* self) {
	FREE(self->vtable);
}

void Timeline_free (Timeline* self) {
	VTABLE(Timeline, self) ->free(self);
}

void Timeline_apply (const Timeline* self, Skeleton* skeleton, float time, float alpha) {
	VTABLE(Timeline, self) ->apply(self, skeleton, time, alpha);
}

/**/

static const float CURVE_LINEAR = 0;
static const float CURVE_STEPPED = -1;
static const int CURVE_SEGMENTS = 10;

void _CurveTimeline_init (CurveTimeline* self, int frameCount) {
	_Timeline_init(SUPER(self));
	self->curves = CALLOC(float, (frameCount - 1) * 6);
}

void _CurveTimeline_deinit (CurveTimeline* self) {
	_Timeline_deinit(SUPER(self));
	FREE(self->curves);
}

void CurveTimeline_setLinear (CurveTimeline* self, int frameIndex) {
	self->curves[frameIndex * 6] = CURVE_LINEAR;
}

void CurveTimeline_setStepped (CurveTimeline* self, int frameIndex) {
	self->curves[frameIndex * 6] = CURVE_STEPPED;
}

void CurveTimeline_setCurve (CurveTimeline* self, int frameIndex, float cx1, float cy1, float cx2, float cy2) {
	float subdiv_step = 1.0f / CURVE_SEGMENTS;
	float subdiv_step2 = subdiv_step * subdiv_step;
	float subdiv_step3 = subdiv_step2 * subdiv_step;
	float pre1 = 3 * subdiv_step;
	float pre2 = 3 * subdiv_step2;
	float pre4 = 6 * subdiv_step2;
	float pre5 = 6 * subdiv_step3;
	float tmp1x = -cx1 * 2 + cx2;
	float tmp1y = -cy1 * 2 + cy2;
	float tmp2x = (cx1 - cx2) * 3 + 1;
	float tmp2y = (cy1 - cy2) * 3 + 1;
	int i = frameIndex * 6;
	self->curves[i] = cx1 * pre1 + tmp1x * pre2 + tmp2x * subdiv_step3;
	self->curves[i + 1] = cy1 * pre1 + tmp1y * pre2 + tmp2y * subdiv_step3;
	self->curves[i + 2] = tmp1x * pre4 + tmp2x * pre5;
	self->curves[i + 3] = tmp1y * pre4 + tmp2y * pre5;
	self->curves[i + 4] = tmp2x * pre5;
	self->curves[i + 5] = tmp2y * pre5;
}

float CurveTimeline_getCurvePercent (const CurveTimeline* self, int frameIndex, float percent) {
	int curveIndex = frameIndex * 6;
	float dfx = self->curves[curveIndex];
	if (dfx == CURVE_LINEAR) return percent;
	if (dfx == CURVE_STEPPED) return 0;
	float dfy = self->curves[curveIndex + 1];
	float ddfx = self->curves[curveIndex + 2];
	float ddfy = self->curves[curveIndex + 3];
	float dddfx = self->curves[curveIndex + 4];
	float dddfy = self->curves[curveIndex + 5];
	float x = dfx, y = dfy;
	int i = CURVE_SEGMENTS - 2;
	while (1) {
		if (x >= percent) {
			float lastX = x - dfx;
			float lastY = y - dfy;
			return lastY + (y - lastY) * (percent - lastX) / (x - lastX);
		}
		if (i == 0) break;
		i--;
		dfx += ddfx;
		dfy += ddfy;
		ddfx += dddfx;
		ddfy += dddfy;
		x += dfx;
		y += dfy;
	}
	return y + (1 - y) * (percent - x) / (1 - x); /* Last point is 1,1. */
}

/* @param target After the first and before the last entry. */
static int binarySearch (float *values, int valuesLength, float target, int step) {
	int low = 0;
	int high = valuesLength / step - 2;
	if (high == 0) return step;
	int current = high >> 1;
	while (1) {
		if (values[(current + 1) * step] <= target)
			low = current + 1;
		else
			high = current;
		if (low == high) return (low + 1) * step;
		current = (low + high) >> 1;
	}
	return 0;
}

/*static int linearSearch (float *values, int valuesLength, float target, int step) {
 int i, last = valuesLength - step;
 for (i = 0; i <= last; i += step) {
 if (values[i] <= target) continue;
 return i;
 }
 return -1;
 }*/

/**/

void _BaseTimeline_free (Timeline* timeline) {
	struct BaseTimeline* self = SUB_CAST(struct BaseTimeline, timeline);
	_CurveTimeline_deinit(SUPER(self));
	FREE(self->frames);
	FREE(self);
}

/* Many timelines have structure identical to struct BaseTimeline and extend CurveTimeline. **/
struct BaseTimeline* _BaseTimeline_new (int frameCount, int frameSize) {
	struct BaseTimeline* self = NEW(struct BaseTimeline);
	_CurveTimeline_init(SUPER(self), frameCount);
	VTABLE(Timeline, self) ->free = _BaseTimeline_free;

	CONST_CAST(int, self->framesLength) = frameCount * frameSize;
	CONST_CAST(float*, self->frames) = CALLOC(float, self->framesLength);

	return self;
}

/**/

static const int ROTATE_LAST_FRAME_TIME = -2;
static const int ROTATE_FRAME_VALUE = 1;

void _RotateTimeline_apply (const Timeline* timeline, Skeleton* skeleton, float time, float alpha) {
	RotateTimeline* self = SUB_CAST(RotateTimeline, timeline);

	if (time < self->frames[0]) return; /* Time is before first frame. */

	Bone *bone = skeleton->bones[self->boneIndex];

	if (time >= self->frames[self->framesLength - 2]) { /* Time is after last frame. */
		float amount = bone->data->rotation + self->frames[self->framesLength - 1] - bone->rotation;
		while (amount > 180)
			amount -= 360;
		while (amount < -180)
			amount += 360;
		bone->rotation += amount * alpha;
		return;
	}

	/* Interpolate between the last frame and the current frame. */
	int frameIndex = binarySearch(self->frames, self->framesLength, time, 2);
	float lastFrameValue = self->frames[frameIndex - 1];
	float frameTime = self->frames[frameIndex];
	float percent = 1 - (time - frameTime) / (self->frames[frameIndex + ROTATE_LAST_FRAME_TIME] - frameTime);
	percent = CurveTimeline_getCurvePercent(SUPER(self), frameIndex / 2 - 1, percent < 0 ? 0 : (percent > 1 ? 1 : percent));

	float amount = self->frames[frameIndex + ROTATE_FRAME_VALUE] - lastFrameValue;
	while (amount > 180)
		amount -= 360;
	while (amount < -180)
		amount += 360;
	amount = bone->data->rotation + (lastFrameValue + amount * percent) - bone->rotation;
	while (amount > 180)
		amount -= 360;
	while (amount < -180)
		amount += 360;
	bone->rotation += amount * alpha;
}

RotateTimeline* RotateTimeline_new (int frameCount) {
	RotateTimeline* self = _BaseTimeline_new(frameCount, 2);
	VTABLE(Timeline, self) ->apply = _RotateTimeline_apply;
	return self;
}

void RotateTimeline_setFrame (RotateTimeline* self, int frameIndex, float time, float angle) {
	frameIndex *= 2;
	self->frames[frameIndex] = time;
	self->frames[frameIndex + 1] = angle;
}

/**/

static const int TRANSLATE_LAST_FRAME_TIME = -3;
static const int TRANSLATE_FRAME_X = 1;
static const int TRANSLATE_FRAME_Y = 2;

void _TranslateTimeline_apply (const Timeline* timeline, Skeleton* skeleton, float time, float alpha) {
	TranslateTimeline* self = SUB_CAST(TranslateTimeline, timeline);

	if (time < self->frames[0]) return; /* Time is before first frame. */

	Bone *bone = skeleton->bones[self->boneIndex];

	if (time >= self->frames[self->framesLength - 3]) { /* Time is after last frame. */
		bone->x += (bone->data->x + self->frames[self->framesLength - 2] - bone->x) * alpha;
		bone->y += (bone->data->y + self->frames[self->framesLength - 1] - bone->y) * alpha;
		return;
	}

	/* Interpolate between the last frame and the current frame. */
	int frameIndex = binarySearch(self->frames, self->framesLength, time, 3);
	float lastFrameX = self->frames[frameIndex - 2];
	float lastFrameY = self->frames[frameIndex - 1];
	float frameTime = self->frames[frameIndex];
	float percent = 1 - (time - frameTime) / (self->frames[frameIndex + TRANSLATE_LAST_FRAME_TIME] - frameTime);
	percent = CurveTimeline_getCurvePercent(SUPER(self), frameIndex / 3 - 1, percent < 0 ? 0 : (percent > 1 ? 1 : percent));

	bone->x += (bone->data->x + lastFrameX + (self->frames[frameIndex + TRANSLATE_FRAME_X] - lastFrameX) * percent - bone->x)
			* alpha;
	bone->y += (bone->data->y + lastFrameY + (self->frames[frameIndex + TRANSLATE_FRAME_Y] - lastFrameY) * percent - bone->y)
			* alpha;
}

TranslateTimeline* TranslateTimeline_new (int frameCount) {
	TranslateTimeline* self = _BaseTimeline_new(frameCount, 3);
	VTABLE(Timeline, self) ->apply = _TranslateTimeline_apply;
	return self;
}

void TranslateTimeline_setFrame (TranslateTimeline* self, int frameIndex, float time, float x, float y) {
	frameIndex *= 3;
	self->frames[frameIndex] = time;
	self->frames[frameIndex + 1] = x;
	self->frames[frameIndex + 2] = y;
}

/**/

void _ScaleTimeline_apply (const Timeline* timeline, Skeleton* skeleton, float time, float alpha) {
	ScaleTimeline* self = SUB_CAST(ScaleTimeline, timeline);

	if (time < self->frames[0]) return; /* Time is before first frame. */

	Bone *bone = skeleton->bones[self->boneIndex];
	if (time >= self->frames[self->framesLength - 3]) { /* Time is after last frame. */
		bone->scaleX += (bone->data->scaleX - 1 + self->frames[self->framesLength - 2] - bone->scaleX) * alpha;
		bone->scaleY += (bone->data->scaleY - 1 + self->frames[self->framesLength - 1] - bone->scaleY) * alpha;
		return;
	}

	/* Interpolate between the last frame and the current frame. */
	int frameIndex = binarySearch(self->frames, self->framesLength, time, 3);
	float lastFrameX = self->frames[frameIndex - 2];
	float lastFrameY = self->frames[frameIndex - 1];
	float frameTime = self->frames[frameIndex];
	float percent = 1 - (time - frameTime) / (self->frames[frameIndex + TRANSLATE_LAST_FRAME_TIME] - frameTime);
	percent = CurveTimeline_getCurvePercent(SUPER(self), frameIndex / 3 - 1, percent < 0 ? 0 : (percent > 1 ? 1 : percent));

	bone->scaleX += (bone->data->scaleX - 1 + lastFrameX + (self->frames[frameIndex + TRANSLATE_FRAME_X] - lastFrameX) * percent
			- bone->scaleX) * alpha;
	bone->scaleY += (bone->data->scaleY - 1 + lastFrameY + (self->frames[frameIndex + TRANSLATE_FRAME_Y] - lastFrameY) * percent
			- bone->scaleY) * alpha;
}

ScaleTimeline* ScaleTimeline_new (int frameCount) {
	ScaleTimeline* self = _BaseTimeline_new(frameCount, 3);
	VTABLE(Timeline, self) ->apply = _ScaleTimeline_apply;
	return self;
}

void ScaleTimeline_setFrame (ScaleTimeline* self, int frameIndex, float time, float x, float y) {
	TranslateTimeline_setFrame(self, frameIndex, time, x, y);
}

/**/

static const int COLOR_LAST_FRAME_TIME = -5;
static const int COLOR_FRAME_R = 1;
static const int COLOR_FRAME_G = 2;
static const int COLOR_FRAME_B = 3;
static const int COLOR_FRAME_A = 4;

void _ColorTimeline_apply (const Timeline* timeline, Skeleton* skeleton, float time, float alpha) {
	ColorTimeline* self = (ColorTimeline*)timeline;

	if (time < self->frames[0]) return; /* Time is before first frame. */

	Slot *slot = skeleton->slots[self->slotIndex];

	if (time >= self->frames[self->framesLength - 5]) { /* Time is after last frame. */
		int i = self->framesLength - 1;
		slot->r = self->frames[i - 3];
		slot->g = self->frames[i - 2];
		slot->b = self->frames[i - 1];
		slot->a = self->frames[i];
		return;
	}

	/* Interpolate between the last frame and the current frame. */
	int frameIndex = binarySearch(self->frames, self->framesLength, time, 5);
	float lastFrameR = self->frames[frameIndex - 4];
	float lastFrameG = self->frames[frameIndex - 3];
	float lastFrameB = self->frames[frameIndex - 2];
	float lastFrameA = self->frames[frameIndex - 1];
	float frameTime = self->frames[frameIndex];
	float percent = 1 - (time - frameTime) / (self->frames[frameIndex + COLOR_LAST_FRAME_TIME] - frameTime);
	percent = CurveTimeline_getCurvePercent(SUPER(self), frameIndex / 5 - 1, percent < 0 ? 0 : (percent > 1 ? 1 : percent));

	float r = lastFrameR + (self->frames[frameIndex + COLOR_FRAME_R] - lastFrameR) * percent;
	float g = lastFrameG + (self->frames[frameIndex + COLOR_FRAME_G] - lastFrameG) * percent;
	float b = lastFrameB + (self->frames[frameIndex + COLOR_FRAME_B] - lastFrameB) * percent;
	float a = lastFrameA + (self->frames[frameIndex + COLOR_FRAME_A] - lastFrameA) * percent;
	if (alpha < 1) {
		slot->r += (r - slot->r) * alpha;
		slot->g += (g - slot->g) * alpha;
		slot->b += (b - slot->b) * alpha;
		slot->a += (a - slot->a) * alpha;
	} else {
		slot->r = r;
		slot->g = g;
		slot->b = b;
		slot->a = a;
	}
}

ColorTimeline* ColorTimeline_new (int frameCount) {
	ColorTimeline* self = (ColorTimeline*)_BaseTimeline_new(frameCount, 5);
	VTABLE(Timeline, self) ->apply = _ColorTimeline_apply;
	return self;
}

void ColorTimeline_setFrame (ColorTimeline* self, int frameIndex, float time, float r, float g, float b, float a) {
	frameIndex *= 5;
	self->frames[frameIndex] = time;
	self->frames[frameIndex + 1] = r;
	self->frames[frameIndex + 2] = g;
	self->frames[frameIndex + 3] = b;
	self->frames[frameIndex + 4] = a;
}

/**/

void _AttachmentTimeline_apply (const Timeline* timeline, Skeleton* skeleton, float time, float alpha) {
	AttachmentTimeline* self = (AttachmentTimeline*)timeline;

	if (time < self->frames[0]) return; /* Time is before first frame. */

	int frameIndex;
	if (time >= self->frames[self->framesLength - 1]) /* Time is after last frame. */
		frameIndex = self->framesLength - 1;
	else
		frameIndex = binarySearch(self->frames, self->framesLength, time, 1) - 1;

	const char* attachmentName = self->attachmentNames[frameIndex];
	Slot_setAttachment(skeleton->slots[self->slotIndex],
			attachmentName ? Skeleton_getAttachmentForSlotIndex(skeleton, self->slotIndex, attachmentName) : 0);
}

void _AttachmentTimeline_free (Timeline* timeline) {
	_Timeline_deinit(timeline);
	AttachmentTimeline* self = (AttachmentTimeline*)timeline;

	int i;
	for (i = 0; i < self->framesLength; ++i)
		FREE(self->attachmentNames[i]);
	FREE(self->attachmentNames);

	FREE(self);
}

AttachmentTimeline* AttachmentTimeline_new (int frameCount) {
	AttachmentTimeline* self = NEW(AttachmentTimeline);
	_Timeline_init(SUPER(self));
	VTABLE(Timeline, self) ->free = _AttachmentTimeline_free;
	VTABLE(Timeline, self) ->apply = _AttachmentTimeline_apply;
	CONST_CAST(char**, self->attachmentNames) = CALLOC(char*, frameCount);

	CONST_CAST(int, self->framesLength) = frameCount;
	CONST_CAST(float*, self->frames) = CALLOC(float, frameCount);

	return self;
}

void AttachmentTimeline_setFrame (AttachmentTimeline* self, int frameIndex, float time, const char* attachmentName) {
	self->frames[frameIndex] = time;
	FREE(self->attachmentNames[frameIndex]);
	if (attachmentName)
		MALLOC_STR(self->attachmentNames[frameIndex], attachmentName);
	else
		self->attachmentNames[frameIndex] = 0;
}
