using System;

static class Program
{
    static int Main(string[] args)
    {
        if (args.Length != 1) throw new Exception("Pass a fixed simulated test frame");
        var beat = BteProtocol.Parse(args[0]);
        if (beat.Session != 10 || beat.EventId != 1 || beat.BeatTimeUs != 1234567 || beat.DetectedTimeUs != 1300000)
            throw new Exception("Wrong payload decoding");
        bool rejected = false;
        try { BteProtocol.Parse(args[0].Substring(0, args[0].Length-4) + "0000"); }
        catch (FormatException) { rejected = true; }
        if (!rejected) throw new Exception("Bad CRC accepted");
        rejected = false;
        try { BteProtocol.Parse("@BTE1,28,00,FFFF"); }
        catch (FormatException) { rejected = true; }
        if (!rejected) throw new Exception("Short payload accepted");
        Console.WriteLine("SIMULATION_ONLY: production C# BteProtocol decoded fixed sample and rejected bad CRC/length.");
        return 0;
    }
}
