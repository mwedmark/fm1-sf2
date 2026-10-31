import struct
import sys
import os
import numpy as np
from sf2utils.sf2parse import Sf2File
from scipy.signal import resample

def convert_to_fm1_format(sf2_path, output_bin_path, target_sample_rate=22050, use_8bit=True):
    print(f"Öppnar Soundfont: {sf2_path}...")
    
    with open(sf2_path, 'rb') as f:
        sf2 = Sf2File(f)
        
        binary_data = bytearray()
        header_entries = []
        current_data_offset = 0  
        processed_count = 0
        
        # Vi går direkt på sf2parser och dess interna shdr-lista (Sample Header)
        if not hasattr(sf2, 'sf2parser') or not hasattr(sf2.sf2parser, 'shdr'):
            print("Fel: Kunde inte hitta råa samplings-headers i Soundfont-filen.")
            return
            
        shdr_table = sf2.sf2parser.shdr
        print(f"Hittade {len(shdr_table)} råa samplings-headers i filen. Startar konvertering...")

        for shdr in shdr_table:
            # shdr är en namngiven struktur direkt från filens parser
            sample_name = shdr.sample_name.decode('utf-8', errors='ignore').strip()
            
            start_pos = shdr.start
            end_pos = shdr.end
            loop_start_pos = shdr.start_loop
            loop_end_pos = shdr.end_loop
            sample_rate = shdr.sample_rate
            original_pitch = shdr.original_pitch

            duration = end_pos - start_pos
            
            # Hoppa över tomma bitar eller metadata-avslutare (EOS)
            if duration <= 48 or not sample_name or sample_name.lower() == 'eos' or sample_name.lower() == 'deadair':
                continue
                
            print(f"Processar ljudvåg: {sample_name} ({duration} samples)")
            
            try:
                # Läs rådata direkt från smpl-offseten i filen (16-bit = 2 bytes per sample)
                pos_in_file = sf2.sf2parser.smpl_offset + (start_pos * 2)
                raw_samples = sf2.sf2parser.read(duration * 2, pos=pos_in_file)
                
                audio_data = np.frombuffer(raw_samples, dtype=np.int16)
                
                if len(audio_data) == 0:
                    continue
                
                # --- 1. Resampling till målfrekvens (t.ex. 22050 Hz) ---
                current_rate = sample_rate if sample_rate > 0 else 44100
                if current_rate != target_sample_rate:
                    num_samples = int(len(audio_data) * target_sample_rate / current_rate)
                    if num_samples > 0:
                        audio_data = resample(audio_data, num_samples).astype(np.int16)
                
                # Beräkna de interna loop-punkterna (relativt till ljudets start som är 0)
                orig_loop_start = loop_start_pos - start_pos
                orig_loop_end = loop_end_pos - start_pos
                
                ratio = target_sample_rate / current_rate
                loop_start = int(orig_loop_start * ratio)
                loop_end = int(orig_loop_end * ratio)
                
                # --- 2. Bitdjup-konvertering (16-bit till 8-bit) ---
                if use_8bit:
                    audio_data = (audio_data // 256).astype(np.int8)
                    sample_bytes = audio_data.tobytes()
                else:
                    sample_bytes = audio_data.tobytes()
                
                data_length = len(sample_bytes)
                
                low_note = 0
                high_note = 127
                root_note = original_pitch if original_pitch > 0 else 60
                has_loop = 1 if (loop_end > loop_start and loop_start >= 0) else 0
                
                # Paketera till den 20-bytes Header-posten
                zone_header = struct.pack('<BBBBIIII', 
                    low_note, high_note, root_note, has_loop,
                    loop_start, loop_end, current_data_offset, data_length
                )
                
                header_entries.append(zone_header)
                binary_data.extend(sample_bytes)
                current_data_offset += data_length
                processed_count += 1
                
            except Exception as e:
                print(f"  Kunde inte processa {sample_name}: {e}")
                continue

        # --- Sammanställ och spara filen ---
        total_zones = len(header_entries)
        final_file = bytearray()
        
        # Skriv totalt antal zoner först (4 bytes)
        final_file.extend(struct.pack('<I', total_zones))
        
        # Skriv alla headers
        for entry in header_entries:
            final_file.extend(entry)
            
        # Skriv ljuddatan
        final_file.extend(binary_data)
        
        with open(output_bin_path, 'wb') as out:
            out.write(final_file)
            
        print(f"\nKlart! Konverterade totalt {processed_count} ljudvågor till '{output_bin_path}'")
        print(f"Total filstorlek på disk: {len(final_file) / 1024:.2f} KB")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Fel: Du måste ange en Soundfont-fil som inparameter!")
        print("Användning: python convert.py [din_fil.sf2]")
        sys.exit(1)
        
    input_sf2 = sys.argv[1] # Hämtar strängen korrekt
    
    if not os.path.exists(input_sf2):
        print(f"Fel: Filen '{input_sf2}' hittades inte.")
        sys.exit(1)
        
    base_name = os.path.splitext(input_sf2)[0]
    output_bin = f"{base_name}_bank.bin"
    
    convert_to_fm1_format(input_sf2, output_bin)
