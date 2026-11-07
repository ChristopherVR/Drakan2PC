// Turns the messages of the parsers (a JSON syntax error, "primary volume descriptor not found") into sentences an author
// can act on. Everything else passes through unchanged. Used by the status line and the toasts.

const RULES: [RegExp, string][] = [
    [
        /primary volume descriptor not found|not an iso|volume descriptor/i,
        "That file is not a disc image the editor can read. Choose the .iso of Drakan: The Ancients' Gates.",
    ],
    [
        /is not valid JSON|Unexpected token|Unexpected end of JSON|JSON\.parse|in JSON at position/i,
        'That file is not a saved level or project: it is not valid JSON. Open a file the editor saved.',
    ],
];

/** The message to show for a raw error message. */
export function friendlyMessage(msg: string): string {
    for (const [re, text] of RULES) if (re.test(msg)) return text;
    return msg;
}
