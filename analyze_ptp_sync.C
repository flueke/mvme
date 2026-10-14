void analyze_ptp_sync()
{
    auto f0 = TFile::Open("ptp_sync_test_switch_250821_170304_raw.root");
    auto f1 = TFile::Open("ptp_sync_test_switch_250821_170311_raw.root");

    auto t0 = f0->Get<TTree>("event0");
    auto t1 = f1->Get<TTree>("event0");

    auto nEntries0 = t0->GetEntries();
    auto nEntries1 = t1->GetEntries();

    std::cout << "Entries in first file: " << nEntries0 << "\n";
    std::cout << "Entries in second file: " << nEntries1 << "\n";

    auto b0 = t0->GetBranch("mvlc_ptp");
    auto b1 = t1->GetBranch("mvlc_ptp");

    event0_modules::Module_mvlc_ptp mvlc_ptp0;
    mvlc_ptp0.InitBranch(b0);

    event0_modules::Module_mvlc_ptp mvlc_ptp1;
    mvlc_ptp1.InitBranch(b1);

    const auto entryCount  = std::min(nEntries0, nEntries1);

    std::cout << "Processing " << entryCount << " entries...\n";

    auto h_dt_s = new TH1D("dt_s", "Delta between ptp s values", 1 << 16, 1, 0);
    auto h_dt_ns = new TH1D("dt_ns", "Delta between ptp ns values", 1 << 16, 1, 0);

    for (auto i=0; i<entryCount; ++i)
    {
        t0->GetEntry(i);
        t1->GetEntry(i);

        auto dt_s = mvlc_ptp1.ptp_s[0] - mvlc_ptp0.ptp_s[0];
        auto dt_ns = mvlc_ptp1.ptp_ns[0] - mvlc_ptp0.ptp_ns[0];

        std::cout << "Entry " << i << ": dt_s = " << dt_s << ", dt_ns = " << dt_ns << "\n";

        h_dt_s->Fill(dt_s);
        h_dt_ns->Fill(dt_ns);
    }

    auto c1 = new TCanvas("c1", "PTP Sync Analysis", 1200, 600);
    c1->Divide(2, 1); // Divide canvas horizontally (2 columns, 1 row)

    // Draw first histogram (dt_s) in left pad
    c1->cd(1);
    h_dt_s->Draw();

    // Draw second histogram (dt_ns) in right pad
    c1->cd(2);
    h_dt_ns->Draw();

    c1->Update();

}
