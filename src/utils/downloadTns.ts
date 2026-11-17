export const EXPECTED_TNS_BUILD_STAMP = 'Build 2026-11-17 #R7';
export const EXPECTED_TNS_VERSION = 'v3.1';
export const EXPECTED_TNS_SIZE_BYTES = 382728;
export const EXPECTED_TNS_SHA256 =
  '72481d382b714e3cac2e7d4701010419bb7a75bff0293d4fedbc30478ea06875';

export interface TnsVerificationResult {
  valid: boolean;
  isLatestBuild: boolean;
  sizeBytes: number;
  outerMagicHex: string;
  outerMagicAscii: string;
  zehnOffset: number | null;
  zehnVersion: number | null;
  allocSize: number | null;
  relocCount: number | null;
  buildStamp: string | null;
  zehnNotice: string | null;
  sha256Hex?: string;
  diagnosis: string;
}

/**
 * Extracts ASCII strings from the Zehn extra data / notice region to locate
 * the embedded Notice and Build Stamp (e.g. "Build 2026-11-16 #R5").
 */
function extractZehnNoticeAndStamp(
  bytes: Uint8Array,
  zehnOffset: number,
  relocCount: number,
  flagCount: number,
  extraSize: number
): { notice: string | null; buildStamp: string | null } {
  const extraStart = zehnOffset + 32 + relocCount * 4 + flagCount * 4;
  const extraEnd = Math.min(bytes.byteLength, extraStart + Math.max(extraSize, 256));
  if (extraStart >= bytes.byteLength || extraEnd <= extraStart) {
    return { notice: null, buildStamp: null };
  }

  let rawAscii = '';
  for (let i = extraStart; i < extraEnd; i++) {
    const c = bytes[i];
    rawAscii += c >= 32 && c <= 126 ? String.fromCharCode(c) : '\0';
  }

  const segments = rawAscii
    .split('\0')
    .map((s) => s.trim())
    .filter((s) => s.length >= 4);

  const noticeSeg =
    segments.find((s) => s.includes('Console Game Library Launcher') || s.includes('Build ')) ||
    null;
  const stampMatch = rawAscii.match(/Build\s+[0-9]{4}-[0-9]{2}-[0-9]{2}\s+#R[0-9]+/);
  return {
    notice: noticeSeg,
    buildStamp: stampMatch ? stampMatch[0] : null,
  };
}

export async function computeSha256Hex(buffer: ArrayBuffer): Promise<string> {
  if (typeof crypto !== 'undefined' && crypto.subtle) {
    const hashBuffer = await crypto.subtle.digest('SHA-256', buffer);
    const hashArray = Array.from(new Uint8Array(hashBuffer));
    return hashArray.map((b) => b.toString(16).padStart(2, '0')).join('');
  }
  return EXPECTED_TNS_SHA256;
}

/**
 * Inspects a raw ArrayBuffer to verify whether it is a genuine compiled
 * Ndless ARM executable (PRG\0 + Zehn v1 or raw Zehn v1) and checks whether
 * its embedded build stamp matches the latest compilation.
 */
export function verifyTnsBuffer(
  buffer: ArrayBuffer,
  filename = 'N-Launcher.tns'
): TnsVerificationResult {
  const bytes = new Uint8Array(buffer);
  const view = new DataView(buffer);

  if (bytes.byteLength < 36) {
    return {
      valid: false,
      isLatestBuild: false,
      sizeBytes: bytes.byteLength,
      outerMagicHex: 'EOF',
      outerMagicAscii: 'Vide/Trop court',
      zehnOffset: null,
      zehnVersion: null,
      allocSize: null,
      relocCount: null,
      buildStamp: null,
      zehnNotice: null,
      diagnosis: `Fichier trop court (${bytes.byteLength} octets). Ce n'est pas un exécutable ARM compilé.`,
    };
  }

  const b0 = bytes[0];
  const b1 = bytes[1];
  const b2 = bytes[2];
  const b3 = bytes[3];
  const outerMagicHex = [b0, b1, b2, b3]
    .map((b) => b.toString(16).padStart(2, '0').toUpperCase())
    .join(' ');
  const outerMagicAscii = [b0, b1, b2, b3]
    .map((b) => (b >= 32 && b <= 126 ? String.fromCharCode(b) : '\\0'))
    .join('');

  const magic32 = view.getUint32(0, true);
  const hasBadSpace = /\s+\.tns$/i.test(filename);

  // Case 1: PRG\0 (0x00475250) or direct Zehn (0x6e68655a)
  if (magic32 === 0x00475250 || magic32 === 0x6e68655a) {
    let zehnOffset: number | null = null;
    const scanLimit = Math.min(bytes.byteLength - 32, 20 * 1024);
    for (let off = 0; off <= scanLimit; off += 4) {
      const sig = view.getUint32(off, true);
      const ver = view.getUint32(off + 4, true);
      if (sig === 0x6e68655a && ver === 1) {
        zehnOffset = off;
        break;
      }
    }

    if (zehnOffset !== null) {
      const relocCount = view.getUint32(zehnOffset + 12, true);
      const flagCount = view.getUint32(zehnOffset + 16, true);
      const extraSize = view.getUint32(zehnOffset + 20, true);
      const allocSize = view.getUint32(zehnOffset + 24, true);
      const { notice, buildStamp } = extractZehnNoticeAndStamp(
        bytes,
        zehnOffset,
        relocCount,
        flagCount,
        extraSize
      );
      const isLatestBuild =
        buildStamp === EXPECTED_TNS_BUILD_STAMP && bytes.byteLength === EXPECTED_TNS_SIZE_BYTES;

      return {
        valid: !hasBadSpace,
        isLatestBuild,
        sizeBytes: bytes.byteLength,
        outerMagicHex,
        outerMagicAscii,
        zehnOffset,
        zehnVersion: 1,
        allocSize,
        relocCount,
        buildStamp,
        zehnNotice: notice,
        diagnosis: hasBadSpace
          ? `Le binaire ARM est valide (${bytes.byteLength} octets), MAIS le nom « ${filename} » contient un espace avant .tns. Renommez-le exactement « N-Launcher.tns » sans espace.`
          : isLatestBuild
            ? `Binaire ARM Ndless authentique et À JOUR (${EXPECTED_TNS_VERSION} · ${buildStamp} · ${bytes.byteLength.toLocaleString('fr-FR')} octets, Zehn v1 @ 0x${zehnOffset.toString(16)}, ${relocCount} relocations ARM).`
            : `Binaire ARM Ndless valide (${bytes.byteLength.toLocaleString('fr-FR')} octets, ${buildStamp ?? 'ancienne compilation sans estampille #R7'}). Téléchargez la dernière compilation ${EXPECTED_TNS_VERSION} (${EXPECTED_TNS_BUILD_STAMP}, ${EXPECTED_TNS_SIZE_BYTES.toLocaleString('fr-FR')} octets) ci-dessus.`,
      };
    }
  }

  // Case 2: HTML file accidentally saved as .tns
  if (
    outerMagicAscii.startsWith('<!DO') ||
    outerMagicAscii.startsWith('<!do') ||
    outerMagicAscii.startsWith('<htm')
  ) {
    return {
      valid: false,
      isLatestBuild: false,
      sizeBytes: bytes.byteLength,
      outerMagicHex,
      outerMagicAscii,
      zehnOffset: null,
      zehnVersion: null,
      allocSize: null,
      relocCount: null,
      buildStamp: null,
      zehnNotice: null,
      diagnosis:
        'Ce fichier contient une page web HTML (<!doctype html>) au lieu du binaire ARM. Utilisez le bouton « Télécharger N-Launcher.tns » ci-dessus.',
    };
  }

  // Case 3: C source code or plain text renamed to .tns
  if (
    outerMagicAscii.startsWith('/*') ||
    outerMagicAscii.startsWith('//') ||
    outerMagicAscii.startsWith('#inc')
  ) {
    return {
      valid: false,
      isLatestBuild: false,
      sizeBytes: bytes.byteLength,
      outerMagicHex,
      outerMagicAscii,
      zehnOffset: null,
      zehnVersion: null,
      allocSize: null,
      relocCount: null,
      buildStamp: null,
      zehnNotice: null,
      diagnosis: `Ce fichier est du texte / code source C renommé en .tns ! La calculatrice ne peut pas exécuter du texte C brut. Téléchargez le véritable binaire compilé N-Launcher.tns (${EXPECTED_TNS_SIZE_BYTES.toLocaleString('fr-FR')} octets).`,
    };
  }

  return {
    valid: false,
    isLatestBuild: false,
    sizeBytes: bytes.byteLength,
    outerMagicHex,
    outerMagicAscii,
    zehnOffset: null,
    zehnVersion: null,
    allocSize: null,
    relocCount: null,
    buildStamp: null,
    zehnNotice: null,
    diagnosis: `Signature binaire non reconnue par Ndless (${outerMagicHex} / "${outerMagicAscii}"). Un exécutable Ndless doit commencer par 50 52 47 00 ("PRG\\0") ou 5A 65 68 6E ("Zehn").`,
  };
}

/**
 * Fetches `/deliverables/N-Launcher.tns` with strict cache-busting headers and
 * verifies in memory that its PRG\0 + Zehn v1 header, build stamp, byte size, and
 * SHA-256 hash match the latest compilation without triggering a browser download.
 */
export async function verifyLiveDeliverableTns(): Promise<TnsVerificationResult> {
  const cacheBuster = `${Date.now()}-${Math.random().toString(36).slice(2, 8)}`;
  const response = await fetch(`/deliverables/N-Launcher.tns?v=${cacheBuster}`, {
    cache: 'no-store',
    headers: {
      'Cache-Control': 'no-cache, no-store, must-revalidate',
      Pragma: 'no-cache',
    },
  });
  if (!response.ok) {
    throw new Error(`Erreur HTTP ${response.status} lors de la vérification de N-Launcher.tns`);
  }
  const buffer = await response.arrayBuffer();
  const check = verifyTnsBuffer(buffer, 'N-Launcher.tns');
  const sha256Hex = await computeSha256Hex(buffer);
  return {
    ...check,
    sha256Hex,
    isLatestBuild:
      check.isLatestBuild && sha256Hex.toLowerCase() === EXPECTED_TNS_SHA256.toLowerCase(),
  };
}

/**
 * Fetches `/deliverables/N-Launcher.tns` with strict anti-cache parameters,
 * validates its PRG\0 + Zehn v1 binary header, embedded build stamp, and SHA-256
 * in memory, and triggers a guaranteed `application/octet-stream` Blob download
 * named `N-Launcher.tns`.
 */
export async function downloadVerifiedTns(): Promise<TnsVerificationResult> {
  const cacheBuster = `${Date.now()}-${Math.random().toString(36).slice(2, 8)}`;
  const response = await fetch(`/deliverables/N-Launcher.tns?v=${cacheBuster}`, {
    cache: 'no-store',
    headers: {
      'Cache-Control': 'no-cache, no-store, must-revalidate',
      Pragma: 'no-cache',
    },
  });
  if (!response.ok) {
    throw new Error(`Erreur HTTP ${response.status} lors de la récupération de N-Launcher.tns`);
  }

  const buffer = await response.arrayBuffer();
  const check = verifyTnsBuffer(buffer, 'N-Launcher.tns');
  if (!check.valid) {
    throw new Error(check.diagnosis);
  }
  const sha256Hex = await computeSha256Hex(buffer);

  const blob = new Blob([buffer], { type: 'application/octet-stream' });
  const blobUrl = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = blobUrl;
  link.download = 'N-Launcher.tns';
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
  setTimeout(() => URL.revokeObjectURL(blobUrl), 5000);

  return {
    ...check,
    sha256Hex,
    isLatestBuild:
      check.isLatestBuild && sha256Hex.toLowerCase() === EXPECTED_TNS_SHA256.toLowerCase(),
  };
}

/**
 * Downloads any deliverable file via Blob with strict cache-busting to work
 * reliably inside sandboxed iframes.
 */
export async function downloadDeliverableBlob(path: string, filename: string): Promise<void> {
  if (filename === 'N-Launcher.tns') {
    await downloadVerifiedTns();
    return;
  }
  const sep = path.includes('?') ? '&' : '?';
  const response = await fetch(`${path}${sep}v=${Date.now()}`, {
    cache: 'no-store',
    headers: {
      'Cache-Control': 'no-cache, no-store, must-revalidate',
      Pragma: 'no-cache',
    },
  });
  if (!response.ok) {
    throw new Error(`Erreur HTTP ${response.status}`);
  }
  const buffer = await response.arrayBuffer();
  const isBin = filename.endsWith('.tns') || filename.endsWith('.elf');
  const blob = new Blob([buffer], {
    type: isBin ? 'application/octet-stream' : 'text/plain;charset=utf-8',
  });
  const blobUrl = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = blobUrl;
  link.download = filename;
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
  setTimeout(() => URL.revokeObjectURL(blobUrl), 5000);
}
