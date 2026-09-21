/*====================================================================
 * bnkf.c
 *
 * Copyright 1993, Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * Port note (GEVR): patch offsets use uintptr_t so sample-table bases are
 * not truncated on 64-bit hosts.
 *====================================================================*/

#include <libaudio.h>
#include <os.h>
#include <ultraerror.h>
#include <stdint.h>

#ifdef GEVR
typedef uintptr_t bnkf_off_t;
#else
typedef s32 bnkf_off_t;
#endif

static void _bnkfPatchBank(ALBank *bank, bnkf_off_t offset, bnkf_off_t table);
static void _bnkfPatchInst(ALInstrument *i, bnkf_off_t offset, bnkf_off_t table);
static void _bnkfPatchSound(ALSound *s, bnkf_off_t offset, bnkf_off_t table);
static void _bnkfPatchWaveTable(ALWaveTable *w, bnkf_off_t offset, bnkf_off_t table);

void alSeqFileNew(ALSeqFile *file, u8 *base)
{
	bnkf_off_t offset = (bnkf_off_t)base;
	s32 i;

	for (i = 0; i < file->seqCount; i++) {
		file->seqArray[i].offset = (u8 *)((u8 *)file->seqArray[i].offset + offset);
	}
}

void alBnkfNew(ALBankFile *file, u8 *table)
{
	bnkf_off_t offset = (bnkf_off_t)file;
	bnkf_off_t woffset = (bnkf_off_t)table;
	s32 i;

	ALFailIf(file->revision != AL_BANK_VERSION, ERR_ALBNKFNEW);

	for (i = 0; i < file->bankCount; i++) {
		file->bankArray[i] = (ALBank *)((u8 *)file->bankArray[i] + offset);
		if (file->bankArray[i]) {
			_bnkfPatchBank(file->bankArray[i], offset, woffset);
		}
	}
}

static void _bnkfPatchBank(ALBank *bank, bnkf_off_t offset, bnkf_off_t table)
{
	s32 i;

	if (bank->flags) {
		return;
	}

	bank->flags = 1;

	if (bank->percussion) {
		bank->percussion = (ALInstrument *)((u8 *)bank->percussion + offset);
		_bnkfPatchInst(bank->percussion, offset, table);
	}

	for (i = 0; i < bank->instCount; i++) {
		bank->instArray[i] = (ALInstrument *)((u8 *)bank->instArray[i] + offset);
		if (bank->instArray[i]) {
			_bnkfPatchInst(bank->instArray[i], offset, table);
		}
	}
}

static void _bnkfPatchInst(ALInstrument *inst, bnkf_off_t offset, bnkf_off_t table)
{
	s32 i;

	if (inst->flags) {
		return;
	}

	inst->flags = 1;

	for (i = 0; i < inst->soundCount; i++) {
		inst->soundArray[i] = (ALSound *)((u8 *)inst->soundArray[i] + offset);
		_bnkfPatchSound(inst->soundArray[i], offset, table);
	}
}

static void _bnkfPatchSound(ALSound *s, bnkf_off_t offset, bnkf_off_t table)
{
	if (s->flags) {
		return;
	}

	s->flags = 1;

	s->envelope = (ALEnvelope *)((u8 *)s->envelope + offset);
	s->keyMap = (ALKeyMap *)((u8 *)s->keyMap + offset);
	s->wavetable = (ALWaveTable *)((u8 *)s->wavetable + offset);
	_bnkfPatchWaveTable(s->wavetable, offset, table);
}

static void _bnkfPatchWaveTable(ALWaveTable *w, bnkf_off_t offset, bnkf_off_t table)
{
	if (w->flags) {
		return;
	}

	w->flags = 1;

#ifdef GEVR
	w->base = (u8 *)((uintptr_t)w->base + table);
#else
	w->base += table;
#endif

	if (w->type == AL_ADPCM_WAVE) {
		w->waveInfo.adpcmWave.book = (ALADPCMBook *)((u8 *)w->waveInfo.adpcmWave.book + offset);
		if (w->waveInfo.adpcmWave.loop) {
			w->waveInfo.adpcmWave.loop = (ALADPCMloop *)((u8 *)w->waveInfo.adpcmWave.loop + offset);
		}
	} else if (w->type == AL_RAW16_WAVE) {
		if (w->waveInfo.rawWave.loop) {
			w->waveInfo.rawWave.loop = (ALRawLoop *)((u8 *)w->waveInfo.rawWave.loop + offset);
		}
	}
}
