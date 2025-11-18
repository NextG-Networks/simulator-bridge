#!/usr/bin/env python3
"""
KPI Visualization Dashboard
Real-time visualization of KPIs from NS3 simulation

Usage:
    pip install streamlit pandas plotly
    streamlit run kpi_dashboard.py
"""

import streamlit as st
import pandas as pd
import plotly.express as px
import plotly.graph_objects as go
from plotly.subplots import make_subplots
import os
import time
from datetime import datetime
from pathlib import Path

# Configuration
# CSV files are in the parent directory (simulator-bridge root)
CSV_GNB_FILE = "../gnb_kpis.csv"
CSV_UE_FILE = "../ue_kpis.csv"
AUTO_REFRESH_INTERVAL = 1.0  # seconds - faster for real-time (must be float for slider)

# Page config
st.set_page_config(
    page_title="NS3 KPI Dashboard - Real-Time",
    page_icon="📊",
    layout="wide",
    initial_sidebar_state="expanded"
)

@st.cache_data(ttl=0.5)  # Very short cache for real-time updates
def load_csv_data():
    """Load CSV files and return dataframes"""
    gnb_df = pd.DataFrame()
    ue_df = pd.DataFrame()
    
    if os.path.exists(CSV_GNB_FILE):
        try:
            gnb_df = pd.read_csv(CSV_GNB_FILE)
            if 'timestamp' in gnb_df.columns:
                gnb_df['timestamp'] = pd.to_datetime(gnb_df['timestamp'], errors='coerce')
        except Exception as e:
            st.error(f"Error loading {CSV_GNB_FILE}: {e}")
    
    if os.path.exists(CSV_UE_FILE):
        try:
            ue_df = pd.read_csv(CSV_UE_FILE)
            if 'timestamp' in ue_df.columns:
                ue_df['timestamp'] = pd.to_datetime(ue_df['timestamp'], errors='coerce')
        except Exception as e:
            st.error(f"Error loading {CSV_UE_FILE}: {e}")
    
    return gnb_df, ue_df

def plot_time_series(df, y_column, title, color=None):
    """Create a time series plot"""
    if df.empty or y_column not in df.columns:
        return None
    
    # Filter out NaN values
    plot_df = df[['timestamp', y_column]].dropna()
    if plot_df.empty:
        return None
    
    fig = px.line(
        plot_df,
        x='timestamp',
        y=y_column,
        title=title,
        labels={'timestamp': 'Time', y_column: y_column.replace('_', ' ')},
        color=color if color and color in df.columns else None
    )
    fig.update_layout(
        hovermode='x unified',
        height=400,
        xaxis_title="Time",
        yaxis_title=y_column.replace('_', ' ')
    )
    return fig

def plot_multi_metric(df, metrics, title, color_col=None):
    """Plot multiple metrics on the same chart"""
    if df.empty:
        return None
    
    fig = make_subplots(
        rows=len(metrics),
        cols=1,
        shared_xaxes=True,
        vertical_spacing=0.05,
        subplot_titles=metrics
    )
    
    for i, metric in enumerate(metrics):
        if metric in df.columns:
            plot_df = df[['timestamp', metric]].dropna()
            if not plot_df.empty:
                fig.add_trace(
                    go.Scatter(
                        x=plot_df['timestamp'],
                        y=plot_df[metric],
                        name=metric,
                        mode='lines+markers',
                        line=dict(width=2)
                    ),
                    row=i+1, col=1
                )
    
    fig.update_layout(
        title=title,
        height=300 * len(metrics),
        showlegend=True,
        hovermode='x unified'
    )
    
    return fig

def main():
    st.title("📊 NS3 KPI Dashboard - Real-Time")
    st.markdown("**Live** visualization of network KPIs from NS3 simulation")
    
    # Load data first
    gnb_df, ue_df = load_csv_data()
    
    # Real-time indicator
    if 'refresh_count' not in st.session_state:
        st.session_state.refresh_count = 0
    st.session_state.refresh_count += 1
    
    # Show refresh indicator
    col1, col2, col3 = st.columns([1, 2, 1])
    with col2:
        st.markdown(f"<div style='text-align: center; padding: 10px; background-color: #e8f5e9; border-radius: 5px;'>"
                   f"🟢 Live | Refresh #{st.session_state.refresh_count} | {datetime.now().strftime('%H:%M:%S')}"
                   f"</div>", unsafe_allow_html=True)
    
    # Sidebar controls
    with st.sidebar:
        st.header("⚙️ Real-Time Controls")
        
        auto_refresh = st.checkbox("🔄 Real-Time Auto-Refresh", value=True)
        if auto_refresh:
            refresh_interval = st.slider("Refresh interval (seconds)", 0.5, 5.0, AUTO_REFRESH_INTERVAL, 0.5)
            st.caption(f"Updating every {refresh_interval}s")
        
        st.markdown("---")
        st.header("📊 Live Status")
        
        # Show last update time
        if 'last_update' not in st.session_state:
            st.session_state.last_update = datetime.now()
        
        time_since_update = (datetime.now() - st.session_state.last_update).total_seconds()
        st.metric("Last Update", f"{time_since_update:.1f}s ago")
        
        # Show data freshness
        if not gnb_df.empty and 'timestamp' in gnb_df.columns:
            latest_time = gnb_df['timestamp'].max()
            if pd.notna(latest_time):
                time_diff = (datetime.now() - latest_time.to_pydatetime()).total_seconds()
                st.metric("Latest Data", f"{time_diff:.1f}s ago")
        
        st.session_state.last_update = datetime.now()
        
        st.markdown("---")
        st.header("📁 Data Files")
        gnb_exists = os.path.exists(CSV_GNB_FILE)
        ue_exists = os.path.exists(CSV_UE_FILE)
        
        st.write(f"**gNB KPIs:** {'✅' if gnb_exists else '❌'} {CSV_GNB_FILE}")
        st.write(f"**UE KPIs:** {'✅' if ue_exists else '❌'} {CSV_UE_FILE}")
        
        if st.button("🔄 Refresh Now"):
            st.cache_data.clear()
            st.rerun()
        
        st.markdown("---")
        st.markdown("### 📊 Available Metrics")
        st.caption("Select metrics to display in the main view")
    
    # Main content tabs
    tab1, tab2, tab3, tab4 = st.tabs(["📡 Cell-Level (gNB)", "📱 UE-Level", "📈 Combined View", "📋 Data Tables"])
    
    # Tab 1: Cell-Level KPIs
    with tab1:
        st.header("Cell-Level KPIs (gNB)")
        
        if gnb_df.empty:
            st.warning(f"No data found in {CSV_GNB_FILE}. Make sure the simulation is running and generating data.")
        else:
            col1, col2, col3 = st.columns(3)
            with col1:
                st.metric("Total Records", len(gnb_df))
            with col2:
                if 'timestamp' in gnb_df.columns:
                    latest_time = gnb_df['timestamp'].max()
                    if pd.notna(latest_time):
                        st.metric("Latest Data", latest_time.strftime('%H:%M:%S'))
            with col3:
                if len(gnb_df) > 1 and 'timestamp' in gnb_df.columns:
                    time_span = (gnb_df['timestamp'].max() - gnb_df['timestamp'].min()).total_seconds()
                    st.metric("Time Span", f"{time_span:.0f}s")
            
            # Get numeric columns (excluding timestamp, meid, cell_id, format)
            numeric_cols = gnb_df.select_dtypes(include=['float64', 'int64']).columns.tolist()
            exclude_cols = ['cell_id']  # Keep cell_id for grouping if needed
            plot_cols = [col for col in numeric_cols if col not in exclude_cols]
            
            if plot_cols:
                # Time series plots for each metric
                selected_metrics = st.multiselect(
                    "Select metrics to display",
                    plot_cols,
                    default=plot_cols[:min(4, len(plot_cols))] if len(plot_cols) > 0 else []
                )
                
                if selected_metrics:
                    # Group by cell_id if available
                    if 'cell_id' in gnb_df.columns:
                        cell_ids = gnb_df['cell_id'].unique()
                        if len(cell_ids) > 1:
                            selected_cells = st.multiselect(
                                "Filter by Cell ID",
                                cell_ids.tolist(),
                                default=cell_ids.tolist()
                            )
                            gnb_df_filtered = gnb_df[gnb_df['cell_id'].isin(selected_cells)]
                        else:
                            gnb_df_filtered = gnb_df
                    else:
                        gnb_df_filtered = gnb_df
                    
                    # Plot selected metrics
                    for metric in selected_metrics:
                        if metric in gnb_df_filtered.columns:
                            fig = plot_time_series(gnb_df_filtered, metric, f"{metric.replace('_', ' ').title()}")
                            if fig:
                                st.plotly_chart(fig, use_container_width=True)
                    
                    # Multi-metric view
                    if len(selected_metrics) > 1:
                        st.subheader("Combined View")
                        fig = plot_multi_metric(gnb_df_filtered, selected_metrics, "Cell-Level KPIs Over Time")
                        if fig:
                            st.plotly_chart(fig, use_container_width=True)
            else:
                st.info("No numeric metrics found in gNB data")
    
    # Tab 2: UE-Level KPIs
    with tab2:
        st.header("UE-Level KPIs")
        
        if ue_df.empty:
            st.warning(f"No data found in {CSV_UE_FILE}. Make sure the simulation is running and generating data.")
        else:
            col1, col2, col3 = st.columns(3)
            with col1:
                st.metric("Total Records", len(ue_df))
            with col2:
                if 'timestamp' in ue_df.columns:
                    latest_time = ue_df['timestamp'].max()
                    if pd.notna(latest_time):
                        st.metric("Latest Data", latest_time.strftime('%H:%M:%S'))
            with col3:
                unique_ues = ue_df['ue_id'].nunique() if 'ue_id' in ue_df.columns else 0
                st.metric("Unique UEs", unique_ues)
            
            # Get numeric columns
            numeric_cols = ue_df.select_dtypes(include=['float64', 'int64']).columns.tolist()
            exclude_cols = ['ue_id', 'cell_id']
            plot_cols = [col for col in numeric_cols if col not in exclude_cols]
            
            if plot_cols:
                selected_metrics = st.multiselect(
                    "Select metrics to display",
                    plot_cols,
                    default=plot_cols[:min(4, len(plot_cols))] if len(plot_cols) > 0 else [],
                    key="ue_metrics"
                )
                
                if selected_metrics:
                    # Filter by UE
                    if 'ue_id' in ue_df.columns:
                        ue_ids = ue_df['ue_id'].unique()
                        selected_ues = st.multiselect(
                            "Filter by UE ID",
                            ue_ids.tolist(),
                            default=ue_ids[:min(5, len(ue_ids))].tolist()  # Show first 5 by default
                        )
                        ue_df_filtered = ue_df[ue_df['ue_id'].isin(selected_ues)]
                    else:
                        ue_df_filtered = ue_df
                    
                    # Filter by cell
                    if 'cell_id' in ue_df_filtered.columns:
                        cell_ids = ue_df_filtered['cell_id'].unique()
                        if len(cell_ids) > 1:
                            selected_cells = st.multiselect(
                                "Filter by Cell ID",
                                cell_ids.tolist(),
                                default=cell_ids.tolist(),
                                key="ue_cells"
                            )
                            ue_df_filtered = ue_df_filtered[ue_df_filtered['cell_id'].isin(selected_cells)]
                    
                    # Plot selected metrics
                    for metric in selected_metrics:
                        if metric in ue_df_filtered.columns:
                            # Group by UE if available
                            color_col = 'ue_id' if 'ue_id' in ue_df_filtered.columns else None
                            fig = plot_time_series(ue_df_filtered, metric, f"{metric.replace('_', ' ').title()}", color=color_col)
                            if fig:
                                st.plotly_chart(fig, use_container_width=True)
                    
                    # Summary statistics
                    st.subheader("Summary Statistics")
                    st.dataframe(ue_df_filtered[selected_metrics].describe(), use_container_width=True)
            else:
                st.info("No numeric metrics found in UE data")
    
    # Tab 3: Combined View
    with tab3:
        st.header("Combined Analysis")
        
        if gnb_df.empty and ue_df.empty:
            st.warning("No data available for combined view")
        else:
            col1, col2 = st.columns(2)
            
            with col1:
                st.subheader("Cell-Level Overview")
                if not gnb_df.empty and 'timestamp' in gnb_df.columns:
                    # Latest values
                    latest_gnb = gnb_df.iloc[-1] if len(gnb_df) > 0 else None
                    if latest_gnb is not None:
                        st.write("**Latest Values:**")
                        for col in gnb_df.select_dtypes(include=['float64', 'int64']).columns:
                            if col != 'cell_id':
                                st.metric(col.replace('_', ' ').title(), f"{latest_gnb[col]:.2f}" if pd.notna(latest_gnb[col]) else "N/A")
            
            with col2:
                st.subheader("UE-Level Overview")
                if not ue_df.empty and 'timestamp' in ue_df.columns:
                    # Latest values per UE
                    latest_ue = ue_df.groupby('ue_id').last() if 'ue_id' in ue_df.columns else ue_df.iloc[-1:]
                    if not latest_ue.empty:
                        st.write("**Latest Values (per UE):**")
                        if isinstance(latest_ue, pd.DataFrame) and len(latest_ue) > 0:
                            # Show first few UEs
                            display_cols = [col for col in ue_df.select_dtypes(include=['float64', 'int64']).columns if col not in ['ue_id', 'cell_id']]
                            st.dataframe(latest_ue[display_cols].head(10), use_container_width=True)
            
            # Correlation analysis
            if not ue_df.empty:
                st.subheader("Metric Correlations")
                numeric_cols = ue_df.select_dtypes(include=['float64', 'int64']).columns.tolist()
                correlation_cols = [col for col in numeric_cols if col not in ['ue_id', 'cell_id']]
                if len(correlation_cols) > 1:
                    corr_matrix = ue_df[correlation_cols].corr()
                    fig = px.imshow(
                        corr_matrix,
                        title="Correlation Matrix",
                        color_continuous_scale='RdBu',
                        aspect="auto"
                    )
                    st.plotly_chart(fig, use_container_width=True)
    
    # Tab 4: Data Tables
    with tab4:
        st.header("Raw Data")
        
        col1, col2 = st.columns(2)
        
        with col1:
            st.subheader("gNB Data")
            if not gnb_df.empty:
                st.dataframe(gnb_df, use_container_width=True, height=400)
                st.download_button(
                    label="📥 Download gNB CSV",
                    data=gnb_df.to_csv(index=False),
                    file_name=f"gnb_kpis_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv",
                    mime="text/csv"
                )
            else:
                st.info("No gNB data available")
        
        with col2:
            st.subheader("UE Data")
            if not ue_df.empty:
                st.dataframe(ue_df, use_container_width=True, height=400)
                st.download_button(
                    label="📥 Download UE CSV",
                    data=ue_df.to_csv(index=False),
                    file_name=f"ue_kpis_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv",
                    mime="text/csv"
                )
            else:
                st.info("No UE data available")
    
    # Real-time auto-refresh
    if auto_refresh:
        # Use placeholder for countdown
        placeholder = st.empty()
        for i in range(int(refresh_interval * 10), 0, -1):
            placeholder.markdown(f"<div style='text-align: center; color: #666;'>"
                               f"Next update in {i/10:.1f}s...</div>", unsafe_allow_html=True)
            time.sleep(0.1)
        placeholder.empty()
        st.rerun()

if __name__ == "__main__":
    main()

