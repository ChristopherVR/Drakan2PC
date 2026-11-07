import { describe, expect, it } from 'vitest';
import { playPackIni, retagManifestToml } from '@drakan/editor-core';
import { PLAY_PACK_ID } from '../src/modio.ts';

describe('the temporary Play pack', () => {
    it('has an id of its own, never the one the importer gives a player-installed pack', () => {
        expect(PLAY_PACK_ID).toBe('editor-play-ootf');
        expect(PLAY_PACK_ID).not.toBe('ootf-levels');
    });

    it('turns the Play pack on and the packs that share its files off, leaving the rest alone', () => {
        const ini = ['; mine', '[mods]', 'drakan-character-motion = on', 'ootf-levels = on', 'OotF-Campaign = on', 'other = off', ''].join(
            '\n',
        );
        const got = playPackIni(ini, PLAY_PACK_ID);
        expect(got).toContain('ootf-levels = off');
        expect(got).toContain('OotF-Campaign = off');
        expect(got).toContain('drakan-character-motion = on');
        expect(got).toContain('other = off');
        expect(got).toContain('editor-play-ootf = on');
        expect(got.startsWith('; mine')).toBe(true);
    });

    it('works on an empty mods.ini and keeps CRLF files CRLF', () => {
        expect(playPackIni('', PLAY_PACK_ID)).toContain('editor-play-ootf = on');
        const crlf = playPackIni('[mods]\r\nootf-levels = on\r\n', PLAY_PACK_ID);
        expect(crlf).toContain('ootf-levels = off\r\n');
    });

    it('gives a copy of a pack manifest another id and name', () => {
        const toml = 'format = 1\nid = "ootf-levels"\nname = "Order of the Flame levels"\nlevels = ["A"]\n';
        const got = retagManifestToml(toml, PLAY_PACK_ID, 'Play copy');
        expect(got).toContain('id = "editor-play-ootf"');
        expect(got).toContain('name = "Play copy"');
        expect(got).toContain('levels = ["A"]');
    });
});
